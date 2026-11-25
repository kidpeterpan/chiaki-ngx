# shellcheck shell=bash
# Pillar 2: static analysis. There is no repo-wide formatter config and no type checker
# for C/C++ beyond the compiler, so the compiler's warnings-as-errors build is in
# build-core. This stage lints what the fork owns and reports the rest as triage.
#
# Thresholds (kept explicit so they cannot drift silently):
#   ShellCheck  warnings and up in the harness and scripts/translator/ fail the stage.
#               Upstream scripts (scripts/*.sh) have pre-existing findings; see TRIAGE.md.
#   actionlint  verify.yml must be clean. Upstream workflows: see TRIAGE.md.
#   qmllint     errors fail the stage; warnings are counted and logged.
#   names       no stray "chiaki-ng" (must be chiaki-ngx); see the allowlist below.

stage_lint() {
	soft_step "shellcheck: harness and fork-owned scripts" lint_shellcheck
	soft_step "actionlint: verify workflow" actionlint .github/workflows/verify.yml
	soft_step "YAML syntax: workflows, packaging, flatpak" lint_yaml
	soft_step "naming: no stray chiaki-ng" lint_names
	soft_step "qmllint: translator QML" lint_qml
	soft_failures_check
}

lint_scripts() {
	git ls-files -co --exclude-standard -- scripts/verify.sh scripts/verify/ scripts/translator/ \
		| grep -E '\.sh$'
}

lint_shellcheck() {
	local files
	files="$(lint_scripts)"
	# shellcheck disable=SC2086
	shellcheck -x -S warning $files
}

lint_yaml() {
	local bad=0 f
	while IFS= read -r f; do
		"$VENV/bin/python" -c 'import sys, yaml; yaml.safe_load(open(sys.argv[1]))' "$f" 2>/dev/null \
			|| { echo "YAML parse error: $f"; bad=1; }
	done < <(git ls-files -co --exclude-standard -- '.github/*.yml' '.github/*.yaml' \
		'packaging/*.yml' 'scripts/flatpak/*.y*ml')
	return "$bad"
}

# Allowed forms of "chiaki-ng": upstream URLs, external image and package IDs, the Flathub
# app ID, the deliberate pre-rename fallback path, and the release-history files.
lint_names() {
	local allow='(?:github\.com[:/]|githubusercontent\.com/)chiaki-ng/chiaki-ng|chiaki-ng\.github\.io/chiaki-ng|chiaki-ng/chiaki-ng|chiaki-ng\.github\.io|chiaki-ng\.com|streetpea/chiaki-ng-builder|StreetPea\.chiaki-ng|streetpea/streetpea/chiaki-ng|chiaki-ngImages|setDesktopFileName\("chiaki-ng"\)|io\.github\.streetpea\.chiaki-ng|share/chiaki-ng/translator'
	local hits
	hits="$(git grep --untracked -n 'chiaki-ng' -- . \
		':!doc/release_notes.md' ':!docs/updates/releases.md' \
		':!docs/translator/PERFORMANCE.md' ':!docs/translator/PRIOR-ART.md' \
		':!gui/io.github.streetpea.Chiaki4deck.appdata.xml' ':!scripts/chiaki-ng.rb' \
		':!scripts/verify.sh' ':!scripts/verify/' \
		| ALLOW="$allow" perl -ne 'my $l = $_; $l =~ s/(?:$ENV{ALLOW})//g; print $_ if $l =~ /chiaki-ng(?!x)/')"
	if [ -n "$hits" ]; then
		echo "stray 'chiaki-ng' (should be chiaki-ngx):"
		echo "$hits"
		return 1
	fi
}

lint_qml() {
	local out errors warnings
	out="$(qmllint gui/src/qml/translator/*.qml gui/src/qml/translator/*.js 2>&1 || true)"
	printf '%s\n' "$out"
	errors="$(printf '%s\n' "$out" | grep -c '^Error:' || true)"
	warnings="$(printf '%s\n' "$out" | grep -c '^Warning:' || true)"
	echo "qmllint: $errors error(s), $warnings warning(s)"
	[ "$errors" -eq 0 ]
}
