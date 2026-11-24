# shellcheck shell=bash
# Pillar 1: environment and configuration integrity. Fails fast, before any build.
#
# Config: chiaki-ngx has no .env file, no config schema and no required runtime keys.
# Settings live in the user's QSettings, which the harness never reads or writes.

stage_env() {
	step "host is macOS with Homebrew" check_host
	step "toolchain present and new enough" check_tools
	step "Homebrew formulas for the production build" check_brew_formulas
	step "submodules initialized at recorded commits" check_submodules
	step "required assets are tracked and present" check_assets
	step "generated protobuf is not committed" check_no_generated
	step "harness Python venv (pinned)" ensure_venv
}

check_host() {
	if [ "$(uname -s)" != Darwin ]; then
		echo "this harness targets macOS with Homebrew (build-release and test-unit use brew paths)"
		return 1
	fi
	command -v brew >/dev/null || { echo "brew not found"; return 1; }
}

check_tools() {
	local missing=0 t ver
	for t in cmake ninja git python3 perl rsync shellcheck actionlint gitleaks osv-scanner; do
		if ! command -v "$t" >/dev/null 2>&1; then
			echo "missing tool: $t"
			missing=1
			continue
		fi
		case "$t" in
			gitleaks) ver="$(gitleaks version 2>/dev/null | head -n 1)" ;;
			*) ver="$("$t" --version 2>/dev/null | head -n 1)" ;;
		esac
		printf '  %-12s %s\n' "$t" "${ver:-unknown}"
	done
	# translator_core needs CMAKE_COMPILE_WARNING_AS_ERROR (CMake 3.24+).
	local cmake_ver major minor
	cmake_ver="$(cmake --version 2>/dev/null | awk 'NR==1{print $3}')"
	major="${cmake_ver%%.*}"
	minor="$(echo "$cmake_ver" | cut -d. -f2)"
	if [ -n "$cmake_ver" ] && { [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 24 ]; }; }; then
		echo "cmake $cmake_ver is older than the required 3.24"
		missing=1
	fi
	[ "$missing" -eq 0 ]
}

# Formulas used by scripts/translator/build-macos.sh (its preflight list) plus the
# OpenSSL 3 and 4 pair the unit-test link workaround needs.
check_brew_formulas() {
	local f missing=0
	for f in qt libplacebo ffmpeg onnxruntime miniupnpc json-c openssl@3 opus; do
		if ! brew list --versions "$f" >/dev/null 2>&1; then
			echo "missing Homebrew formula: $f"
			missing=1
		fi
	done
	if ! brew list --versions sdl2 >/dev/null 2>&1 && ! brew list --versions sdl2-compat >/dev/null 2>&1; then
		echo "missing Homebrew formula: sdl2 (or sdl2-compat)"
		missing=1
	fi
	if ! brew list --versions openssl@4 >/dev/null 2>&1; then
		echo "note: openssl@4 not installed; the unit-test link workaround is skipped"
	fi
	[ "$missing" -eq 0 ]
}

# `git submodule status` prefixes: '-' not initialized, '+' checked out at a different
# commit than recorded, 'U' merge conflict. Any of these fails.
check_submodules() {
	local bad
	bad="$(git submodule status | grep -E '^[-+U]' || true)"
	if [ -n "$bad" ]; then
		echo "submodules not at their recorded commits (run: git submodule update --init --recursive):"
		echo "$bad"
		return 1
	fi
	echo "  $(git submodule status | wc -l | tr -d ' ') submodules at recorded commits"
}

check_assets() {
	local p missing=0
	for p in \
		assets/models/ch_PP-OCRv3_det.onnx \
		assets/models/japan_PP-OCRv3_rec.onnx \
		assets/models/japan_dict.txt \
		assets/dict/jmdict.sample.db \
		translator_core/tests/fixtures/subtitle.ppm; do
		if [ -z "$(git ls-files -- "$p")" ] || [ ! -f "$p" ]; then
			echo "missing or untracked: $p"
			missing=1
		fi
	done
	[ "$missing" -eq 0 ]
}

check_no_generated() {
	local hits
	hits="$(git ls-files | grep -E '(^|/)takion\.pb\.[ch]$' || true)"
	if [ -n "$hits" ]; then
		echo "generated protobuf is committed (see CONTRIBUTOR_GUIDE / copilot-instructions):"
		echo "$hits"
		return 1
	fi
}
