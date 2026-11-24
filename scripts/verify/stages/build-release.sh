# shellcheck shell=bash
# Pillar 3 (production): the shipping app builds clean in Release with the translator.
# The harness only ever deletes build-release/, never the user's build-macos/.

stage_build_release() {
	if [ "$VERIFY_CLEAN" = 1 ]; then
		step "clean build-release" rm -rf build-release
	fi
	case "$PLATFORM" in
		Darwin) step "production build (Release, GUI, translator, ONNX)" build_release_macos ;;
		Linux)  step "production build (Release, GUI, translator)" build_release_linux ;;
	esac
	step "app binary present and executable" test -x "$(platform_app_binary)"
}

# macOS delegates to the repo's own production recipe, so the harness and the shipped
# build cannot drift. BUILD_DIR keeps it out of the user's build-macos/.
build_release_macos() {
	env BUILD_DIR="$ROOT/build-release" scripts/translator/build-macos.sh
}

# Linux has no equivalent script, so the options mirror build-macos.sh. ONNX Runtime is not
# packaged by most distributions: set ONNXRUNTIME_ROOT to enable OCR, otherwise the app is
# built without it and the OCR smoke probe falls back to the build-core binary.
build_release_linux() {
	local args=(
		-DCMAKE_BUILD_TYPE=Release
		-DCHIAKI_ENABLE_CLI=OFF
		-DCHIAKI_ENABLE_TESTS=OFF
		-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF
		-DCHIAKI_ENABLE_SETSU=OFF
		-DCHIAKI_ENABLE_RUDP=OFF
		-DCHIAKI_ENABLE_TRANSLATOR=ON
	)
	if [ -n "${ONNXRUNTIME_ROOT:-}" ] && [ -d "${ONNXRUNTIME_ROOT}/include/onnxruntime" ]; then
		args+=(-DTRANS_ENABLE_ONNXRUNTIME=ON "-DONNXRUNTIME_ROOT=$ONNXRUNTIME_ROOT")
	else
		echo "ONNXRUNTIME_ROOT not set or incomplete; building without the OCR backend"
		args+=(-DTRANS_ENABLE_ONNXRUNTIME=OFF)
	fi
	cmake -S . -B build-release -G Ninja "${args[@]}"
	cmake --build build-release --target chiaki -j"$(platform_cpus)"
}
