# shellcheck shell=bash
# Shared plumbing for scripts/verify.sh: logging, stage runner, bounded processes,
# process-group cleanup, failure excerpts, summary and JSON. Sourced, never executed.
#
# Bash 3.2 compatible (macOS /bin/bash): no associative arrays, mapfile or ${x,,}.

# shellcheck source=platform.sh
. "$ROOT/scripts/verify/platform.sh"

now() { perl -MTime::HiRes=time -e 'printf "%.3f\n", time'; }
secs_between() { perl -e 'printf "%.1f", $ARGV[1] - $ARGV[0]' "$1" "$2"; }

# say writes to the report stream: stdout normally, stderr when --json owns stdout.
say() { printf '%s\n' "$*" >&"$REPORT_FD"; }

die() { echo "verify: $*" >&2; exit 2; }

# ---- stage plan -----------------------------------------------------------------

ALL_STAGES="env lint build-core test-core build-release test-unit smoke security"

# env gates every stage: it is what validates the prerequisites a stage needs, so running
# any stage without it turns a missing tool into a bare "command not found".
deps_of() {
	case "$1" in
		env) echo "" ;;
		lint|build-core|build-release|test-unit|security) echo "env" ;;
		test-core) echo "build-core" ;;
		smoke) echo "build-core build-release" ;;
		*) echo "unknown stage: $1" >&2; return 1 ;;
	esac
}

# Adds a stage and its prerequisites to PLAN, in canonical order, without duplicates.
PLAN=""
plan_add() {
	local s="$1" d
	case " $PLAN " in *" $s "*) return 0 ;; esac
	for d in $(deps_of "$s"); do plan_add "$d"; done
	PLAN="$PLAN $s"
}

# Orders PLAN by the canonical stage order so output is deterministic.
ordered_plan() {
	local s out=""
	for s in $ALL_STAGES; do
		case " $PLAN " in *" $s "*) out="$out $s" ;; esac
	done
	echo "$out"
}

# ---- per-stage timeouts ---------------------------------------------------------
# A hung compiler or scanner must not block a run forever. Generous enough that a slow
# but working machine never trips them; override with VERIFY_TIMEOUT_<STAGE> in seconds.
stage_timeout() {
	local override
	override="$(eval "echo \${VERIFY_TIMEOUT_$(echo "$1" | tr 'a-z-' 'A-Z_'):-}")"
	if [ -n "$override" ]; then echo "$override"; return; fi
	case "$1" in
		env|lint|smoke) echo 600 ;;
		build-core|test-core) echo 1200 ;;
		test-unit|security) echo 1800 ;;
		build-release) echo 3600 ;;
		*) echo 1800 ;;
	esac
}

# ---- run lock --------------------------------------------------------------------
# Concurrent runs would share build-core/, build-unit/ and build-release/ and corrupt
# each other. mkdir is atomic, so it is the lock; a lock whose owner is gone is stale.
LOCK_DIR="$ROOT/.verify-logs/lock"
LOCK_HELD=0

acquire_lock() {
	mkdir -p "$ROOT/.verify-logs"
	if mkdir "$LOCK_DIR" 2>/dev/null; then
		echo $$ >"$LOCK_DIR/pid"
		LOCK_HELD=1
		return 0
	fi
	local owner
	owner="$(cat "$LOCK_DIR/pid" 2>/dev/null || true)"
	if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
		echo "verify: another run is in progress (pid $owner); wait for it or kill it" >&2
		return 1
	fi
	echo "verify: clearing a stale lock from pid ${owner:-unknown}" >&2
	rm -rf "$LOCK_DIR"
	mkdir "$LOCK_DIR" 2>/dev/null || { echo "verify: could not take the lock" >&2; return 1; }
	echo $$ >"$LOCK_DIR/pid"
	LOCK_HELD=1
}

release_lock() {
	[ "$LOCK_HELD" -eq 1 ] || return 0
	rm -rf "$LOCK_DIR"
	LOCK_HELD=0
}

# ---- reproducibility ---------------------------------------------------------------
# Exact tool versions, so a disagreement between a developer machine and CI is visible in
# the logs instead of being guessed at. See TRIAGE T11.
record_tool_versions() {
	local out="$RUN_DIR/tool-versions.txt" t ver
	{
		echo "platform: $PLATFORM $(uname -r) $(uname -m)"
		for t in $(platform_tools); do
			if command -v "$t" >/dev/null 2>&1; then
				case "$t" in
					gitleaks) ver="$(gitleaks version 2>&1 | head -n 1)" ;;
					brew) ver="$(brew --version 2>&1 | head -n 1)" ;;
					*) ver="$("$t" --version 2>&1 | head -n 1)" ;;
				esac
			else
				ver="MISSING"
			fi
			printf '%-12s %s\n' "$t" "$ver"
		done
	} >"$out" 2>&1
}

# ---- process tracking -----------------------------------------------------------

# Process-group ids of every stage we started. Killed on exit or interrupt.
TRACKED_PGIDS=""
track_pgid() { TRACKED_PGIDS="$TRACKED_PGIDS $1"; }

# Runs "$@" with a wall-clock limit (seconds). Exit 124 on timeout, the same code as
# coreutils timeout(1), which macOS does not ship. perl's alarm survives exec, so this
# stays one process: no watchdog to orphan and no pipe held open.
bounded() {
	local limit="$1"; shift
	local rc=0
	perl -e 'alarm shift; exec @ARGV or die "exec failed: $!\n"' "$limit" "$@" || rc=$?
	# SIGALRM's default action terminates the process; the shell reports 128+14.
	if [ "$rc" -eq 142 ]; then return 124; fi
	return "$rc"
}

# step "label" cmd args...  Records the current step for failure reports, then runs it.
# Runs inside a stage subshell with set -e, so a failing step fails the stage.
# Exits the stage subshell on failure explicitly. Do not rely on set -e: bash ignores
# errexit inside any function or subshell that runs in an || or if context, which
# silently passed failing steps before this was made explicit.
_run_step() {
	local label="$1" rc=0; shift
	printf '%s\t%s\n' "$label" "$*" >"$VERIFY_STEP_FILE"
	printf '\n### %s\n$ %s\n' "$label" "$*"
	"$@" || rc=$?
	if [ "$rc" -ne 0 ]; then printf 'check failed (exit %s): %s\n' "$rc" "$label"; fi
	return "$rc"
}

# Fail-fast step: a failure ends the stage. Use for build chains, where later steps
# cannot mean anything once an earlier one failed.
step() {
	local rc=0
	_run_step "$@" || rc=$?
	if [ "$rc" -ne 0 ]; then exit "$rc"; fi
}

# Independent check: a failure is recorded and the stage keeps going, so one run reports
# every problem. The stage must end with soft_failures_check.
SOFT_FAILED=""
soft_step() {
	local label="$1" rc=0; shift
	_run_step "$label" "$@" || rc=$?
	if [ "$rc" -ne 0 ]; then SOFT_FAILED="${SOFT_FAILED}${SOFT_FAILED:+; }$label"; fi
	return 0
}

soft_failures_check() {
	if [ -n "$SOFT_FAILED" ]; then
		printf 'failed checks: %s\n' "$SOFT_FAILED"
		printf 'failed checks: %s\n' "$SOFT_FAILED" >"$VERIFY_STEP_FILE"
		exit 1
	fi
}

# Private Python venv for the harness (YAML checks, nanopb generator). Pinned by
# scripts/verify/requirements.txt and kept under build-verify/ so the system Python
# is never modified.
VENV="$ROOT/build-verify/venv"
ensure_venv() {
	if "$VENV/bin/python" -c 'import yaml, google.protobuf, grpc_tools' 2>/dev/null; then
		return 0
	fi
	python3 -m venv "$VENV" && "$VENV/bin/pip" install -q -r "$ROOT/scripts/verify/requirements.txt"
}

# Kills every tracked process group, escalating to SIGKILL. Echoes the number of
# groups that were still alive after the grace period (orphans).
kill_tracked() {
	local pgid orphans=0 alive=""
	for pgid in $TRACKED_PGIDS; do
		if kill -0 -- "-$pgid" 2>/dev/null; then alive="$alive $pgid"; fi
	done
	if [ -z "$alive" ]; then echo 0; return 0; fi
	TRACKED_PGIDS="$alive"
	for pgid in $TRACKED_PGIDS; do
		kill -TERM -- "-$pgid" 2>/dev/null || true
	done
	sleep 1
	for pgid in $TRACKED_PGIDS; do
		if kill -0 -- "-$pgid" 2>/dev/null; then
			kill -KILL -- "-$pgid" 2>/dev/null || true
		fi
	done
	sleep 0.5
	for pgid in $TRACKED_PGIDS; do
		if kill -0 -- "-$pgid" 2>/dev/null; then orphans=$((orphans + 1)); fi
	done
	echo "$orphans"
}

# ---- stage execution ------------------------------------------------------------

# Ordered plan of stages to run. Filled by plan_add, read by verify.sh.
PLAN=""

# Records one result row: stage, status, seconds, exit, log, failing step.
RESULTS_FILE=""
export CURRENT_STAGE=""
record_result() { printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" "$6" >>"$RESULTS_FILE"; }

# Runs one stage in its own process group, logging everything to the stage log.
# Returns the stage's exit status.
run_stage() {
	local stage="$1"
	local fn
	fn="stage_$(echo "$stage" | tr '-' '_')"
	local log="$RUN_DIR/$stage.log"
	local step_file="$RUN_DIR/$stage.step"
	local start end rc pgid

	: >"$log"
	: >"$step_file"
	CURRENT_STAGE="$stage"
	say "> $stage"
	start="$(now)"

	# Job control gives the stage its own process group, so kill_tracked reaches
	# every child (cmake, ninja, ...) even if the stage is interrupted.
	local limit timeout_flag watchdog
	limit="$(stage_timeout "$stage")"
	timeout_flag="$RUN_DIR/$stage.timed-out"
	rm -f "$timeout_flag"

	set -m
	(
		set -Eeuo pipefail
		export VERIFY_STEP_FILE="$step_file" VERIFY_LOG="$log"
		# shellcheck source=/dev/null
		. "$ROOT/scripts/verify/stages/$stage.sh"
		"$fn"
	) >>"$log" 2>&1 &
	pgid=$!
	track_pgid "$pgid"
	# Watchdog: kills the stage's whole process group if it outlives its budget. Tracked
	# too, so an interrupt cleans it up; cancelled below on the normal path.
	( sleep "$limit"; : >"$timeout_flag"; kill -TERM -- "-$pgid" 2>/dev/null ) &
	watchdog=$!
	track_pgid "$watchdog"
	wait "$pgid" 2>/dev/null
	rc=$?
	kill -TERM -- "-$watchdog" 2>/dev/null || true
	wait "$watchdog" 2>/dev/null || true
	set +m

	if [ -f "$timeout_flag" ]; then
		rc=124
		echo "stage exceeded its ${limit}s budget and was terminated" >>"$log"
	fi
	CURRENT_STAGE=""

	end="$(now)"
	local took; took="$(secs_between "$start" "$end")"
	local step=""
	[ -s "$step_file" ] && step="$(cut -f2- "$step_file" | head -n 1)"
	if [ "$rc" -eq 0 ]; then
		say "  PASS $stage  ${took}s"
		record_result "$stage" PASS "$took" 0 "$log" ""
	elif [ "$rc" -eq 124 ]; then
		say "  FAIL $stage  ${took}s  (timed out after $(stage_timeout "$stage")s)"
		record_result "$stage" FAIL "$took" 124 "$log" "timed out: ${step:-stage}"
		print_failure "$stage" "$rc" "${step:-timed out}" "$log"
	else
		say "  FAIL $stage  ${took}s  (exit $rc)"
		record_result "$stage" FAIL "$took" "$rc" "$log" "$step"
		print_failure "$stage" "$rc" "$step" "$log"
	fi
	return "$rc"
}

# Prints at most 80 lines of failure context: the failing command, matching error
# lines (capped), and the log tail. The full log path is always printed.
print_failure() {
	local stage="$1" rc="$2" step="$3" log="$4"
	local matches
	matches="$(grep -n -i -E 'error|fatal|failed|undefined|assert|not found|no such file|traceback|exception|panic|segmentation|abort' "$log" \
		| grep -v -i 'warning:' | head -n 55 | cut -c1-200 || true)"
	say "    failing step: ${step:-<none recorded>}"
	say "    --- excerpt (max ~80 lines) ---"
	if [ -n "$matches" ]; then
		printf '%s\n' "$matches" | sed 's/^/    /' >&"$REPORT_FD"
		say "    ... tail ..."
	fi
	tail -n 20 "$log" | cut -c1-200 | sed 's/^/    /' >&"$REPORT_FD"
	say "    full log: ${log#"$ROOT"/}"
}

# ---- summary --------------------------------------------------------------------

print_summary() {
	local total="$1" orphans="$2" overall="$3"
	say ""
	say "== verification summary =="
	say "mode: $MODE   run: $RUN_ID   logs: ${RUN_DIR#"$ROOT"/}"
	printf '%-14s %-6s %9s %5s\n' stage status seconds exit >&"$REPORT_FD"
	local stage status took code
	while IFS="$(printf '\t')" read -r stage status took code _ _; do
		printf '%-14s %-6s %9s %5s\n' "$stage" "$status" "$took" "$code" >&"$REPORT_FD"
	done <"$RESULTS_FILE"
	say "cleanup: $orphans orphaned process group(s) after exit"
	say "RESULT: $overall   total ${total}s"
}

write_json() {
	local total="$1" orphans="$2" overall="$3" out="$RUN_DIR/summary.json"
	python3 - "$RESULTS_FILE" "$out" "$MODE" "$RUN_ID" "$total" "$orphans" "$overall" <<'PY'
import json, sys
results, out, mode, run_id, total, orphans, overall = sys.argv[1:]
stages = []
with open(results) as fh:
    for line in fh:
        stage, status, took, code, log, step = line.rstrip("\n").split("\t")
        stages.append({
            "name": stage,
            "status": status,
            "duration_s": float(took),
            "exit_code": int(code),
            "log": log,
            "failing_step": step or None,
        })
doc = {
    "status": overall,
    "mode": mode,
    "run_id": run_id,
    "elapsed_s": float(total),
    "orphaned_process_groups": int(orphans),
    "stages": stages,
}
with open(out, "w") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
PY
	if [ "$JSON" -eq 1 ]; then cat "$out"; fi
}
