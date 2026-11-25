# shellcheck shell=bash
# Pillar 6: security and hygiene.
#   tracked   no credential-shaped files tracked (names, not contents)
#   secrets   gitleaks over tracked and untracked, non-ignored files; allowlist in
#             scripts/verify/gitleaks.toml, each entry with a reason
#   audit     osv-scanner over the repo, including pinned submodules. Fails on HIGH or
#             CRITICAL. Severity is the advisory's own label, else the CVSS base score
#             osv-scanner reports, in the CVSS v3 bands (9.0+ CRITICAL, 7.0+ HIGH): many
#             advisories, the submodule CVEs among them, carry only a score. Entries with
#             neither are counted, not failed. A HIGH or CRITICAL finding may be accepted
#             only by an unexpired entry in scripts/verify/audit-exceptions.txt. Scanner
#             failures fail the stage.

stage_security() {
	soft_step "no credential-shaped files tracked" sec_tracked_files
	soft_step "secret scan: gitleaks (tracked + untracked, redacted)" sec_gitleaks
	soft_step "dependency audit: osv-scanner (fail on HIGH/CRITICAL)" sec_osv
	soft_failures_check
}

sec_tracked_files() {
	local hits
	hits="$(git ls-files | grep -E -i '(^|/)(\.env(\..*)?|secret\.tar|keystore-env\.sh|chiaki\.conf|.*\.(jks|p12|pfx|pem|key))$' || true)"
	if [ -n "$hits" ]; then
		echo "credential-shaped files are tracked:"
		echo "$hits"
		return 1
	fi
}

sec_gitleaks() {
	local staging="$TMPDIR/gitleaks-staging" list="$TMPDIR/gitleaks-files.txt"
	rm -rf "$staging"
	mkdir -p "$staging" || return 1
	# Snapshot the files that would be committed: tracked plus untracked, not ignored.
	# Entries deleted from the working tree but still in the index are dropped first.
	# Build trees and .verify-logs/ are excluded by .gitignore, so they are not scanned.
	git ls-files -co --exclude-standard | while IFS= read -r f; do
		if [ -e "$f" ]; then printf '%s\n' "$f"; fi
	done >"$list"
	if ! rsync -a --files-from="$list" ./ "$staging/"; then
		echo "snapshot copy failed; refusing to scan an incomplete tree"
		return 1
	fi
	# A scan of a near-empty snapshot would pass for the wrong reason. Guard it.
	if [ ! -f "$staging/README.md" ] || [ "$(find "$staging" -type f | wc -l | tr -d ' ')" -lt 100 ]; then
		echo "snapshot looks incomplete; refusing a false pass"
		return 1
	fi
	local rc=0
	gitleaks dir "$staging" --config "$ROOT/scripts/verify/gitleaks.toml" \
		--redact --no-banner --exit-code 1 \
		--report-format json --report-path "$RUN_DIR/gitleaks.json" || rc=$?
	local count
	count="$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$RUN_DIR/gitleaks.json" 2>/dev/null || echo "?")"
	echo "  gitleaks findings: $count (values redacted; see ${RUN_DIR#"$ROOT"/}/gitleaks.json)"
	python3 - "$RUN_DIR/gitleaks.json" <<'PY' 2>/dev/null || true
import json, sys
for f in json.load(open(sys.argv[1])):
    print(f"  {f['RuleID']}  {f['File'].split('/gitleaks-staging/')[-1]}:{f['StartLine']}")
PY
	rm -rf "$staging"
	return "$rc"
}

sec_osv() {
	local rc=0
	osv-scanner scan source --recursive --format json \
		--output-file "$RUN_DIR/osv.json" "$ROOT" || rc=$?
	# osv-scanner exits 1 when it found vulnerabilities; that is data, not a failure.
	if [ "$rc" -ne 0 ] && [ "$rc" -ne 1 ]; then
		echo "osv-scanner failed (exit $rc); the audit did not run"
		return 1
	fi
	[ -s "$RUN_DIR/osv.json" ] || { echo "osv-scanner produced no report"; return 1; }
	python3 - "$RUN_DIR/osv.json" "$ROOT/scripts/verify/audit-exceptions.txt" "$ROOT" <<'PY'
import datetime, json, sys
report, exceptions_path, root = sys.argv[1:]
today = datetime.date.today()

exceptions = {}
with open(exceptions_path) as fh:
    for raw in fh:
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split(None, 2)
        if len(parts) < 3 or not parts[1].startswith("expires="):
            print(f"  malformed exception line (need: ID expires=YYYY-MM-DD reason): {raw.strip()}")
            sys.exit(1)
        exceptions[parts[0]] = (datetime.date.fromisoformat(parts[1][len("expires="):]), parts[2])

def cvss_band(score):
    if score >= 9.0:
        return "CRITICAL"
    if score >= 7.0:
        return "HIGH"
    if score >= 4.0:
        return "MEDIUM"
    return "LOW" if score > 0 else ""


def severity_of(vuln, scores):
    """The advisory's own label, else the band of the CVSS score osv-scanner computed."""
    label = ((vuln.get("database_specific") or {}).get("severity") or "").upper()
    if label:
        return ("MEDIUM" if label == "MODERATE" else label), "label"
    for key in [vuln.get("id"), *vuln.get("aliases", [])]:
        try:
            score = float(scores.get(key, ""))
        except ValueError:
            continue
        return cvss_band(score), f"CVSS {score}"
    return "", ""


blocking, excepted, unrated = [], [], 0
for result in json.load(open(report)).get("results", []):
    source = result.get("source", {}).get("path", "").replace(root + "/", "")
    for pkg in result.get("packages", []):
        name = pkg.get("package", {}).get("name") or "(submodule commit)"
        version = pkg.get("package", {}).get("version", "")
        scores = {i: group.get("max_severity", "") for group in pkg.get("groups", []) for i in group.get("ids", [])}
        for vuln in pkg.get("vulnerabilities", []):
            vid = vuln.get("id", "?")
            aliases = vuln.get("aliases", [])
            severity, basis = severity_of(vuln, scores)
            where = f"{name} {version} [{source}] ({basis})".replace("  ", " ")
            if severity in ("HIGH", "CRITICAL"):
                key = next((k for k in [vid, *aliases] if k in exceptions), None)
                if key is None:
                    blocking.append(f"{vid} {severity} {where}")
                    continue
                expires, reason = exceptions[key]
                if expires < today:
                    blocking.append(f"{vid} {severity} {where}: exception for {key} expired {expires}")
                else:
                    excepted.append(f"{vid} {severity} {where}: accepted until {expires} ({reason})")
            elif not severity:
                unrated += 1

print(f"  packages with advisories: {sum(1 for r in json.load(open(report)).get('results', []) for p in r.get('packages', []) if p.get('vulnerabilities'))}")
print(f"  unrated (no label and no CVSS score, counted not failed): {unrated}")
for line in excepted:
    print(f"  accepted: {line}")
for line in blocking[:40]:
    print(f"  BLOCKING: {line}")
if len(blocking) > 40:
    print(f"  ... {len(blocking) - 40} more blocking")
sys.exit(1 if blocking else 0)
PY
}
