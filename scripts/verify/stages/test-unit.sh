# shellcheck shell=bash
# Pillar 4 (lib/): munit suites for the C protocol library (chiaki-unit), with the GUI,
# CLI and translator off. Steam Deck native (HIDAPI/FFTW) and RUDP are off because they
# need extra libraries that the unit tests do not use.
#
# The macOS OpenSSL 3/4 workaround lives in platform_openssl_args (see TRIAGE T3). On Linux
# the distribution's single OpenSSL is used and no extra arguments are needed.

stage_test_unit() {
	if [ "$VERIFY_CLEAN" = 1 ]; then
		step "clean build-unit" rm -rf build-unit
	fi
	step "harness venv (nanopb generator)" ensure_venv
	local ssl_args=()
	local line
	while IFS= read -r line; do
		[ -n "$line" ] && ssl_args+=("$line")
	done < <(platform_openssl_args)
	step "configure chiaki-lib tests (GUI, CLI, translator off)" \
		cmake -S . -B build-unit -G Ninja \
			-DCMAKE_BUILD_TYPE=Release -DCHIAKI_ENABLE_TESTS=ON \
			-DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_TRANSLATOR=OFF \
			-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_SETSU=OFF -DCHIAKI_ENABLE_RUDP=OFF \
			-DPYTHON_EXECUTABLE="$VENV/bin/python" -DPython3_EXECUTABLE="$VENV/bin/python" \
			${ssl_args[@]+"${ssl_args[@]}"}
	step "build chiaki-unit" cmake --build build-unit --target chiaki-unit
	step "ctest chiaki-unit" ctest --test-dir build-unit --no-tests=error --output-on-failure
}
