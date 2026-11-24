# shellcheck shell=bash
# Pillar 5: runtime smoke. The service-style checks (health endpoints, migrations,
# ephemeral databases) do not apply to a desktop client, so this runs the real binaries:
#
#   ocr    translator_ocr_probe on the tracked fixture; asserts the expected text
#   dict   translator_dict_probe on the tracked sample database; asserts a known gloss
#   app    the shipping app's --help; asserts it starts, dyld resolves every library,
#          and no preference file of the user's changed
#
# Not covered, deliberately: a windowed launch. `--profile` persistently changes the
# user's current profile (gui/src/main.cpp, Settings::SetCurrentProfile), and a timed
# kill during Vulkan startup latches the OpenGL fallback (qmlmainwindow.cpp). Both would
# alter the real app state, so the windowed boot is reported as not covered.

SMOKE_OCR_TEXT="ゲームを続けよう"
SMOKE_DICT_GLOSS="repetition mark in katakana"
SMOKE_TIMEOUT=30

stage_smoke() {
	soft_step "OCR probe: expected text, confidence >= 0.9" smoke_ocr
	soft_step "dictionary probe: known headword gloss" smoke_dict
	soft_step "app --help: starts, loads libraries, touches no user state" smoke_app_help
	soft_failures_check
}

smoke_ocr() {
	local out rc=0
	out="$(bounded "$SMOKE_TIMEOUT" build-core/translator_ocr_probe \
		assets/models/ch_PP-OCRv3_det.onnx assets/models/japan_PP-OCRv3_rec.onnx \
		assets/models/japan_dict.txt translator_core/tests/fixtures/subtitle.ppm 2>&1)" || rc=$?
	printf '%s\n' "$out"
	[ "$rc" -eq 0 ] || { echo "probe exited $rc"; return 1; }
	case "$out" in
		*"text=$SMOKE_OCR_TEXT"*) ;;
		*) echo "expected text not recognized: $SMOKE_OCR_TEXT"; return 1 ;;
	esac
	local conf
	conf="$(printf '%s\n' "$out" | sed -n 's/.*conf=\([0-9.]*\).*/\1/p' | head -n 1)"
	awk -v c="${conf:-0}" 'BEGIN { exit !(c >= 0.9) }' || { echo "confidence too low: ${conf:-none}"; return 1; }
	echo "  recognized '$SMOKE_OCR_TEXT' at confidence $conf"
}

smoke_dict() {
	local out rc=0
	out="$(bounded "$SMOKE_TIMEOUT" build-core/translator_dict_probe \
		assets/dict/jmdict.sample.db exact "ヽ" 2>&1)" || rc=$?
	printf '%s\n' "$out"
	[ "$rc" -eq 0 ] || { echo "probe exited $rc"; return 1; }
	case "$out" in
		*"$SMOKE_DICT_GLOSS"*) echo "  gloss found: $SMOKE_DICT_GLOSS" ;;
		*) echo "expected gloss not found: $SMOKE_DICT_GLOSS"; return 1 ;;
	esac
}

# Snapshot of the user's preference files that the app would write. Compared before and
# after --help; any change fails the stage.
prefs_snapshot() {
	local d
	for d in "$HOME/Library/Preferences" "$HOME/Library/Application Support"; do
		[ -d "$d" ] || continue
		find "$d" -maxdepth 2 -iname '*chiaki*' -exec stat -f '%N %m %z' {} \; 2>/dev/null
	done | sort
}

smoke_app_help() {
	local app="build-release/gui/chiaki.app/Contents/MacOS/chiaki"
	local before after out rc=0
	before="$(prefs_snapshot)"
	# offscreen: QGuiApplication needs no display. --help exits in main.cpp before any
	# Settings object is created.
	out="$(QT_QPA_PLATFORM=offscreen bounded "$SMOKE_TIMEOUT" "$app" --help 2>&1)" || rc=$?
	printf '%s\n' "$out" | head -n 20 || true
	[ "$rc" -eq 0 ] || { echo "app --help exited $rc (124 = timed out)"; return 1; }
	case "$out" in
		*"--profile"*) echo "  --help lists the expected options" ;;
		*) echo "--help output did not list --profile"; return 1 ;;
	esac
	after="$(prefs_snapshot)"
	if [ "$before" != "$after" ]; then
		echo "user preference files changed during --help:"
		diff <(printf '%s\n' "$before") <(printf '%s\n' "$after") || true
		return 1
	fi
	echo "  no user preference files changed"
}
