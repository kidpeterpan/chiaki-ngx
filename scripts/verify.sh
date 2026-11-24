#!/usr/bin/env bash
# Single entry point for verifying that chiaki-ngx is healthy.
#
#   scripts/verify.sh [--full | --quick | --stage NAME ...] [--json] [--clean]
#
#   --full          every stage, clean builds (default; use before opening a PR)
#   --quick         inner loop: env, lint, build-core, test-core (incremental)
#   --stage NAME    one stage plus its prerequisites; repeatable
#   --json          machine-readable summary on stdout (progress goes to stderr)
#   --clean         wipe harness-owned build dirs before building (implied by --full)
#
# Stages: env lint build-core test-core build-release test-unit smoke security
#
# Exit codes: 0 PASS, 1 a verification stage failed, 2 environment prerequisite missing
#             (the env stage failed), 64 usage error, 130/143 interrupted.
# Logs: .verify-logs/<run-id>/<stage>.log (latest run: .verify-logs/latest).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 2
# shellcheck source=verify/lib.sh
. "$ROOT/scripts/verify/lib.sh"

usage() {
	sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
	exit "${1:-64}"
}

MODE=""
REQUESTED=""
JSON=0
CLEAN=0
while [ $# -gt 0 ]; do
	case "$1" in
		--full)  [ -z "$MODE" ] || [ "$MODE" = full ]  || usage; MODE=full ;;
		--quick) [ -z "$MODE" ] || [ "$MODE" = quick ] || usage; MODE=quick ;;
		--stage)
			shift; [ $# -gt 0 ] || { echo "--stage needs a name" >&2; usage; }
			deps_of "$1" >/dev/null 2>&1 || { echo "unknown stage: $1" >&2; usage; }
			REQUESTED="$REQUESTED $1"; MODE=stage ;;
		--json)  JSON=1 ;;
		--clean) CLEAN=1 ;;
		-h|--help) usage 0 ;;
		*) echo "unknown argument: $1" >&2; usage ;;
	esac
	shift
done
[ -n "$MODE" ] || MODE=full
[ "$MODE" = full ] && CLEAN=1
case "$MODE" in
	full)  REQUESTED="$ALL_STAGES" ;;
	quick) REQUESTED="env lint build-core test-core" ;;
	stage) ;;
esac
[ "$MODE" = stage ] && [ -z "$REQUESTED" ] && usage

TRANS_ONNX="${TRANS_ONNX:-ON}"
case "$TRANS_ONNX" in ON|OFF) ;; *) echo "TRANS_ONNX must be ON or OFF" >&2; exit 64 ;; esac

export REPORT_FD=1
[ "$JSON" -eq 1 ] && REPORT_FD=2

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
RUN_DIR="$ROOT/.verify-logs/$RUN_ID"
mkdir -p "$RUN_DIR"
ln -sfn "$RUN_ID" "$ROOT/.verify-logs/latest"
RESULTS_FILE="$RUN_DIR/results.tsv"
: >"$RESULTS_FILE"

# Per-run temp dir: stages point TMPDIR here so tests never write into the repo or
# the shared /tmp. Removed on exit.
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/chiaki-verify.XXXXXX")"
export TMPDIR="$TMP_DIR" TZ=UTC
export ROOT RUN_DIR RUN_ID MODE TRANS_ONNX JSON
export VERIFY_CLEAN="$CLEAN"

INTERRUPTED=""
FINISHED=0
START_ALL="$(now)"

finish() {
	[ "$FINISHED" -eq 1 ] && return
	FINISHED=1
	local total orphans overall
	total="$(secs_between "$START_ALL" "$(now)")"
	orphans=0
	if [ -n "$TRACKED_PGIDS" ]; then
		orphans="$(kill_tracked)"
	fi
	rm -rf "$TMP_DIR"
	if [ -n "$INTERRUPTED" ]; then
		overall="INTERRUPTED"
		if [ -n "$CURRENT_STAGE" ]; then
			record_result "$CURRENT_STAGE" INTERRUPTED "$(secs_between "$STAGE_START" "$(now)")" "$INTERRUPTED" "$RUN_DIR/$CURRENT_STAGE.log" ""
		fi
	elif awk -F'\t' '$2=="FAIL"||$2=="SKIP"{f=1} END{exit !f}' "$RESULTS_FILE" || [ "$orphans" -gt 0 ]; then
		overall="FAIL"
	else
		overall="PASS"
	fi
	print_summary "$total" "$orphans" "$overall"
	write_json "$total" "$orphans" "$overall"
	prune_old_runs
	if [ -n "$INTERRUPTED" ]; then exit "$INTERRUPTED"; fi
	if [ "$overall" = PASS ]; then exit 0; fi
	if awk -F'\t' '$1=="env" && ($2=="FAIL"||$2=="SKIP"){f=1} END{exit !f}' "$RESULTS_FILE"; then exit 2; fi
	exit 1
}

# Keeps the 20 most recent run directories so .verify-logs/ does not grow forever.
prune_old_runs() {
	local dirs
	dirs="$(cd "$ROOT/.verify-logs" && ls -1dt */ 2>/dev/null | tail -n +21 || true)"
	[ -n "$dirs" ] || return 0
	printf '%s\n' "$dirs" | while read -r d; do rm -rf "$ROOT/.verify-logs/${d%/}"; done
}

trap 'INTERRUPTED=130; exit 130' INT
trap 'INTERRUPTED=143; exit 143' TERM
trap finish EXIT

say "verify: mode=$MODE run=$RUN_ID clean=$CLEAN TRANS_ONNX=$TRANS_ONNX"

for s in $REQUESTED; do plan_add "$s"; done
FAILED_STAGES=""
failed() { case " $FAILED_STAGES " in *" $1 "*) return 0 ;; esac; return 1; }

for s in $(ordered_plan); do
	blocked=""
	for d in $(deps_of "$s"); do
		if failed "$d"; then blocked="$d"; fi
	done
	if [ -n "$blocked" ]; then
		say "  SKIP $s  (prerequisite $blocked failed)"
		record_result "$s" SKIP 0.0 -1 "" "prerequisite $blocked failed"
		FAILED_STAGES="$FAILED_STAGES $s"
		continue
	fi
	# Not "run_stage || ...": that would put the stage in an errexit-ignored context.
	STAGE_START="$(now)"
	run_stage "$s"
	if [ $? -ne 0 ]; then FAILED_STAGES="$FAILED_STAGES $s"; fi
done

# Exit path (and the summary) is handled by the EXIT trap.
exit 0
