# shellcheck shell=bash
# Pillar 3 (translator_core): the Qt-free core builds with warnings as errors.
# The compiler is the type checker for C/C++, so this is the static-typing gate.
# Clean in --full; incremental in --quick.

stage_build_core() {
	if [ "$VERIFY_CLEAN" = 1 ]; then
		step "clean build-core" rm -rf build-core
	fi
	step "configure translator_core (warnings as errors)" \
		cmake -S translator_core -B build-core -G Ninja \
			-DTRANS_BUILD_TESTS=ON \
			-DTRANS_ENABLE_ONNXRUNTIME="$TRANS_ONNX" \
			-DCMAKE_COMPILE_WARNING_AS_ERROR=ON \
			-DCMAKE_CXX_FLAGS="-Wall -Wextra"
	step "build translator_core" cmake --build build-core
}
