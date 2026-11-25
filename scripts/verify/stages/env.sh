# shellcheck shell=bash
# Pillar 1: environment and configuration integrity. Fails fast, before any build.
# Platform differences come from scripts/verify/platform.sh.
#
# Config: chiaki-ngx has no .env file, no config schema and no required runtime keys.
# Settings live in the user's QSettings, which the harness never reads or writes.

stage_env() {
	step "supported platform" check_host
	step "toolchain present and new enough" check_tools
	step "library prerequisites" check_libs
	step "submodules initialized at recorded commits" check_submodules
	step "required assets are tracked and present" check_assets
	step "generated protobuf is not committed" check_no_generated
	step "harness Python venv (pinned)" ensure_venv
}

check_host() {
	if ! platform_supported; then
		echo "unsupported platform: $PLATFORM (this harness supports macOS and Linux)"
		return 1
	fi
	echo "  $PLATFORM $(uname -m)"
	if [ "$PLATFORM" = Darwin ] && ! command -v brew >/dev/null 2>&1; then
		echo "brew not found; install Homebrew or run on Linux"
		return 1
	fi
}

# Tools required by the stages in this run (VERIFY_PLAN), not every tool the harness knows.
required_tools() {
	local s out
	out="$(platform_base_tools)"
	for s in ${VERIFY_PLAN:-}; do
		out="$out $(stage_tools "$s")"
	done
	# shellcheck disable=SC2086
	printf '%s\n' $out | sort -u
}

check_tools() {
	local missing=0 t
	for t in $(required_tools); do
		if ! command -v "$t" >/dev/null 2>&1; then
			echo "missing tool: $t"
			missing=1
		fi
	done
	# Versions are recorded per run in tool-versions.txt; see TRIAGE T11.
	sed 's/^/  /' "$RUN_DIR/tool-versions.txt" 2>/dev/null || true
	# translator_core needs CMAKE_COMPILE_WARNING_AS_ERROR (CMake 3.24+).
	local cmake_ver major minor
	cmake_ver="$(cmake --version 2>/dev/null | awk 'NR==1{print $3}')"
	if [ -n "$cmake_ver" ] && command -v cmake >/dev/null 2>&1; then
		major="${cmake_ver%%.*}"
		minor="$(echo "$cmake_ver" | cut -d. -f2)"
		if [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 24 ]; }; then
			echo "cmake $cmake_ver is older than the required 3.24"
			missing=1
		fi
	fi
	if [ "$missing" -ne 0 ]; then
		echo "install with:"
		platform_install_hint | sed 's/^/  /'
	fi
	[ "$missing" -eq 0 ]
}

# Only the libraries the planned stages link against; see stage_libs.
required_libs() {
	local s out=""
	for s in ${VERIFY_PLAN:-}; do
		out="$out $(stage_libs "$s")"
	done
	# shellcheck disable=SC2086
	printf '%s\n' $out | sort -u
}

check_libs() {
	local l missing=0 any=0
	for l in $(required_libs); do
		any=1
		if platform_lib_present "$l"; then
			printf '  %-14s ok\n' "$l"
		else
			echo "missing: $l"
			missing=1
		fi
	done
	if plan_needs_app; then
		any=1
		if platform_sdl_present; then
			printf '  %-14s ok\n' sdl2
		else
			echo "missing: sdl2 (or sdl2-compat)"
			missing=1
		fi
		if [ "$PLATFORM" = Darwin ] && ! brew list --versions openssl@4 >/dev/null 2>&1; then
			echo "  note: openssl@4 absent; the unit-test link workaround is skipped (TRIAGE T3)"
		fi
	fi
	[ "$any" -eq 0 ] && echo "  (no library prerequisites for the requested stages)"
	if [ "$missing" -ne 0 ]; then
		echo "install with:"
		platform_install_hint | sed 's/^/  /'
	fi
	[ "$missing" -eq 0 ]
}

# `git submodule status` prefixes: '-' not initialized, '+' checked out at a different
# commit than recorded, 'U' merge conflict. Any of these fails.
check_submodules() {
	local bad
	bad="$(git submodule status --recursive | grep -E '^[-+U]' || true)"
	if [ -n "$bad" ]; then
		echo "submodules not at their recorded commits (run: git submodule update --init --recursive):"
		echo "$bad"
		return 1
	fi
	echo "  $(git submodule status --recursive | wc -l | tr -d ' ') submodules at recorded commits"
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
