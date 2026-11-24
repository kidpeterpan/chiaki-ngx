# shellcheck shell=bash
# Pillar 4 (lib/): munit suites for the C protocol library (chiaki-unit), with the GUI,
# CLI and translator off. Steam Deck native (HIDAPI/FFTW) and RUDP are off because they
# need extra libraries that the unit tests do not use.
#
# Known pre-existing issue (see TRIAGE.md): the bundled curl needs OpenSSL 3 headers and
# libcrypto, while lib/src/aia_openssl.c needs OpenSSL 4's libcrypto symbols. The
# workaround below links both, mirroring scripts/translator/build-macos.sh.

openssl_args() {
	local ssl3 ssl4
	ssl3="$(brew --prefix openssl@3 2>/dev/null)" && [ -d "$ssl3" ] || return 0
	printf '%s\n' \
		"-DOPENSSL_ROOT_DIR=$ssl3" \
		"-DOPENSSL_INCLUDE_DIR=$ssl3/include" \
		"-DOPENSSL_CRYPTO_LIBRARY=$ssl3/lib/libcrypto.dylib" \
		"-DOPENSSL_SSL_LIBRARY=$ssl3/lib/libssl.dylib" \
		"-DCMAKE_C_FLAGS=-I$ssl3/include" \
		"-DCMAKE_CXX_FLAGS=-I$ssl3/include"
	ssl4="$(brew --prefix openssl@4 2>/dev/null || true)"
	if [ -n "$ssl4" ] && [ -f "$ssl4/lib/libcrypto.dylib" ]; then
		printf '%s\n' "-DCMAKE_EXE_LINKER_FLAGS=$ssl4/lib/libcrypto.dylib"
	fi
}

stage_test_unit() {
	if [ "$VERIFY_CLEAN" = 1 ]; then
		step "clean build-unit" rm -rf build-unit
	fi
	step "harness venv (nanopb generator)" ensure_venv
	local ssl_args=()
	local line
	while IFS= read -r line; do
		[ -n "$line" ] && ssl_args+=("$line")
	done < <(openssl_args)
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
