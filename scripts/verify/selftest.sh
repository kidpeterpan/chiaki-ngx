#!/usr/bin/env bash
# Tests for the verification harness itself: the contract AGENTS.md documents (exit codes,
# prerequisite skips, timeouts, the run lock, interrupts, the JSON summary, log pruning).
# It runs the real scripts/verify.sh with stand-in stages (VERIFY_STAGE_DIR) and a private
# log root (VERIFY_LOG_ROOT), so it builds nothing and never touches .verify-logs/ or the
# lock of a real run.
#
#   scripts/verify/selftest.sh       exits 0 when every case passes, 1 otherwise
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERIFY="$ROOT/scripts/verify.sh"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/verify-selftest.XXXXXX")"
STAGES="$WORK/stages"
OUT="$WORK/out"
export VERIFY_STAGE_DIR="$STAGES" VERIFY_LOG_ROOT="$WORK/logs"
# A sleep length no other process is likely to use, so leftovers can be found and killed.
LONG_SLEEP=23.71
FAILURES=0

cleanup() {
	pkill -f "sleep $LONG_SLEEP" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT

# One stand-in per real stage. Each passes, unless FAKE_FAIL names it; FAKE_SLEEP_<STAGE>
# makes it run for that many seconds first.
make_stages() {
	local stages stage
	stages="$(sed -n 's/^ALL_STAGES="\(.*\)"$/\1/p' "$ROOT/scripts/verify/lib.sh")"
	mkdir -p "$STAGES"
	for stage in $stages; do
		cat >"$STAGES/$stage.sh" <<EOF
# shellcheck shell=bash
stage_$(printf '%s' "$stage" | tr '-' '_')() { fake_stage $stage; }

fake_stage() {
	local var
	var="FAKE_SLEEP_\$(printf '%s' "\$1" | tr 'a-z-' 'A-Z_')"
	if [ -n "\${!var:-}" ]; then step "sleep" sleep "\${!var}"; fi
	case " \${FAKE_FAIL:-} " in *" \$1 "*) step "planned failure" false ;; esac
	step "planned pass" true
}
EOF
	done
}

# Runs verify.sh with the given arguments; prints its exit code. Output goes to $OUT.
run_verify() {
	local rc=0
	"$VERIFY" "$@" >"$OUT" 2>&1 || rc=$?
	echo "$rc"
}

# Readers for the latest run's summary.json.
summary() {
	python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' \
		"$VERIFY_LOG_ROOT/latest/summary.json" "$1"
}

stage_names() {
	python3 -c 'import json, sys; print(" ".join(s["name"] for s in json.load(open(sys.argv[1]))["stages"]))' \
		"$VERIFY_LOG_ROOT/latest/summary.json"
}

stage_field() {
	python3 -c 'import json, sys; print(next(s[sys.argv[3]] for s in json.load(open(sys.argv[1]))["stages"] if s["name"] == sys.argv[2]))' \
		"$VERIFY_LOG_ROOT/latest/summary.json" "$1" "$2"
}

check() {
	local name="$1" expected="$2" actual="$3"
	if [ "$expected" = "$actual" ]; then
		echo "PASS  $name"
		return
	fi
	echo "FAIL  $name: expected '$expected', got '$actual'"
	tail -n 12 "$OUT" 2>/dev/null | sed 's/^/        /'
	FAILURES=$((FAILURES + 1))
}

wait_for_lock_owner() {
	local tries=0
	while [ ! -s "$VERIFY_LOG_ROOT/run.lock.pid" ] && [ "$tries" -lt 50 ]; do
		sleep 0.1
		tries=$((tries + 1))
	done
}

# Waits until the stage's own process is running, not just the harness: a case about what
# stages inherit means nothing if the harness is stopped before any stage started.
wait_for_stage_sleeping() {
	local tries=0
	while ! grep -q '^### sleep' "$VERIFY_LOG_ROOT/latest/$1.log" 2>/dev/null && [ "$tries" -lt 100 ]; do
		sleep 0.1
		tries=$((tries + 1))
	done
}

test_usage_errors() {
	check "unknown argument is a usage error" 64 "$(run_verify --bogus)"
	check "unknown stage is a usage error" 64 "$(run_verify --stage nope)"
	check "malformed timeout override is a usage error" 64 "$(VERIFY_TIMEOUT_LINT=abc run_verify --quick)"
	check "--help exits 0" 0 "$(run_verify --help)"
}

test_quick_passes() {
	check "--quick exits 0 when every stage passes" 0 "$(run_verify --quick)"
	check "--quick runs exactly the inner-loop stages" "env lint build-core test-core" \
		"$(stage_names)"
	check "summary status is PASS" PASS "$(summary status)"
}

test_json_owns_stdout() {
	"$VERIFY" --quick --json >"$WORK/stdout.json" 2>"$OUT"
	check "--json prints only the JSON summary on stdout" PASS \
		"$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["status"])' "$WORK/stdout.json" 2>&1)"
}

test_failure_does_not_stop_independent_stages() {
	local status
	export FAKE_FAIL="lint"
	status="$(run_verify --full)"
	unset FAKE_FAIL
	check "a failed stage exits 1" 1 "$status"
	check "the failed stage is FAIL" FAIL "$(stage_field lint status)"
	check "the failing step is recorded" false "$(stage_field lint failing_step)"
	check "an independent later stage still runs" PASS "$(stage_field security status)"
}

test_prerequisite_failure_skips_dependents() {
	local status
	export FAKE_FAIL="env"
	status="$(run_verify --quick)"
	unset FAKE_FAIL
	check "a failed env exits 2" 2 "$status"
	check "stages after a failed env are skipped" SKIP "$(stage_field lint status)"
	export FAKE_FAIL="build-core"
	status="$(run_verify --quick)"
	unset FAKE_FAIL
	check "a failed build-core exits 1" 1 "$status"
	check "test-core is skipped when build-core fails" SKIP "$(stage_field test-core status)"
}

test_timeout_terminates_stage() {
	local start took
	start="$(date +%s)"
	check "a timed-out stage exits 1" 1 "$(FAKE_SLEEP_LINT=$LONG_SLEEP VERIFY_TIMEOUT_LINT=1 run_verify --stage lint)"
	took=$(($(date +%s) - start))
	check "the timed-out stage reports exit 124" 124 "$(stage_field lint exit_code)"
	check "the run ends soon after the budget" yes "$([ "$took" -lt 15 ] && echo yes || echo "no (${took}s)")"
	check "no process group is left behind" 0 "$(summary orphaned_process_groups)"
}

test_lock_blocks_a_second_run() {
	FAKE_SLEEP_LINT=3 "$VERIFY" --stage lint >"$WORK/first.out" 2>&1 &
	local first=$!
	wait_for_lock_owner
	check "a second run exits 2 while the lock is held" 2 "$(run_verify --stage env)"
	check "the second run names the owner" yes "$(grep -q 'another run is in progress' "$OUT" && echo yes || echo no)"
	wait "$first"
	check "the first run still passes" 0 "$?"
	check "the lock is free once the first run ends" 0 "$(run_verify --stage env)"
}

test_killed_run_frees_lock() {
	FAKE_SLEEP_LINT=$LONG_SLEEP "$VERIFY" --stage lint >"$WORK/killed.out" 2>&1 &
	local victim=$!
	wait_for_lock_owner
	wait_for_stage_sleeping lint
	kill -KILL "$victim" 2>/dev/null
	wait "$victim" 2>/dev/null || true
	# The stage's watchdog notices the dead harness within a second or two and ends the
	# stage; only then is the lock released, so poll instead of trying once.
	local status tries=0
	status="$(run_verify --stage env)"
	while [ "$status" = 2 ] && [ "$tries" -lt 20 ]; do
		sleep 0.5
		status="$(run_verify --stage env)"
		tries=$((tries + 1))
	done
	check "a run killed with SIGKILL does not leave the lock held" 0 "$status"
	check "the killed run's stage is ended, not left building" no \
		"$(pgrep -f "sleep $LONG_SLEEP" >/dev/null && echo yes || echo no)"
	pkill -f "sleep $LONG_SLEEP" 2>/dev/null || true
}

# Background jobs of a non-interactive shell ignore SIGINT unless job control is on, and
# an ignored signal cannot be trapped, so this case turns job control on like a terminal.
test_interrupt_cleans_up() {
	local signal expected victim
	for signal in INT TERM; do
		expected=130
		[ "$signal" = TERM ] && expected=143
		set -m
		FAKE_SLEEP_LINT=$LONG_SLEEP "$VERIFY" --stage lint >"$OUT" 2>&1 &
		victim=$!
		set +m
		wait_for_lock_owner
		wait_for_stage_sleeping lint
		kill -"$signal" "$victim"
		wait "$victim"
		check "SIG$signal exits $expected" "$expected" "$?"
		check "SIG$signal marks the run INTERRUPTED" INTERRUPTED "$(summary status)"
		check "SIG$signal leaves no process group behind" 0 "$(summary orphaned_process_groups)"
	done
}

test_prune_keeps_recent_runs_only() {
	local i
	mkdir -p "$VERIFY_LOG_ROOT/keep-me"
	for i in $(seq 1 25); do
		mkdir -p "$VERIFY_LOG_ROOT/20200101-0000$(printf '%02d' "$i")-$i"
		touch -t "202001010000.$(printf '%02d' "$i")" "$VERIFY_LOG_ROOT/20200101-0000$(printf '%02d' "$i")-$i"
	done
	run_verify --stage env >/dev/null
	check "at most 20 run directories are kept" 20 \
		"$(cd "$VERIFY_LOG_ROOT" && ls -1d [0-9]*-[0-9]*-[0-9]*/ | wc -l | tr -d ' ')"
	check "directories that are not runs are left alone" yes "$([ -d "$VERIFY_LOG_ROOT/keep-me" ] && echo yes || echo no)"
	check "the latest link survives pruning" yes "$([ -f "$VERIFY_LOG_ROOT/latest/summary.json" ] && echo yes || echo no)"
}

make_stages
test_usage_errors
test_quick_passes
test_json_owns_stdout
test_failure_does_not_stop_independent_stages
test_prerequisite_failure_skips_dependents
test_timeout_terminates_stage
test_lock_blocks_a_second_run
test_killed_run_frees_lock
test_interrupt_cleans_up
test_prune_keeps_recent_runs_only

echo
if [ "$FAILURES" -eq 0 ]; then
	echo "selftest: all cases passed"
	exit 0
fi
echo "selftest: $FAILURES case(s) failed"
exit 1
