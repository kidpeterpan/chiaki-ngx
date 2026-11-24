# shellcheck shell=bash
# Pillar 3 (production): the shipping app builds clean in Release with the translator,
# ONNX Runtime and the bundled models. Delegates to scripts/translator/build-macos.sh,
# the repo's own production recipe, so the harness and the release build cannot drift.
# The harness only ever deletes build-release/, never the user's build-macos/.

stage_build_release() {
	if [ "$VERIFY_CLEAN" = 1 ]; then
		step "clean build-release" rm -rf build-release
	fi
	step "production build (Release, GUI, translator, ONNX)" \
		env BUILD_DIR="$ROOT/build-release" scripts/translator/build-macos.sh
	step "app binary present and executable" \
		test -x build-release/gui/chiaki.app/Contents/MacOS/chiaki
}
