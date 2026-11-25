# shellcheck shell=bash
# Pillar 4 (gui/ translator): the Qt-side translator suites that neither test-core
# (translator_core only) nor test-unit (lib/, GUI off) reach: cloud backends, cloud OCR,
# shortcuts, preferences, overlay geometry and the controller-driven settings panel (QML).
#
# Configures the project with tests on but builds only the translator test targets, so no
# app, curl or FFmpeg code is compiled. The targets are read back from CTest, so a new
# translator suite is built and run without editing this file.

GUI_TEST_DIR=build-gui-tests
QML_SUITE=translator_qml_tests

stage_test_gui() {
	if [ "$VERIFY_CLEAN" = 1 ]; then
		step "clean $GUI_TEST_DIR" rm -rf "$GUI_TEST_DIR"
	fi
	step "harness venv (nanopb generator)" ensure_venv
	step "configure translator GUI tests" \
		cmake -S . -B "$GUI_TEST_DIR" -G Ninja \
			-DCMAKE_BUILD_TYPE=Release -DCHIAKI_ENABLE_TESTS=ON \
			-DCHIAKI_ENABLE_GUI=ON -DCHIAKI_ENABLE_TRANSLATOR=ON -DCHIAKI_ENABLE_CLI=OFF \
			-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_SETSU=OFF -DCHIAKI_ENABLE_RUDP=OFF \
			-DTRANS_ENABLE_ONNXRUNTIME="$TRANS_ONNX" \
			-DPYTHON_EXECUTABLE="$VENV/bin/python" -DPython3_EXECUTABLE="$VENV/bin/python"
	step "QML suite registered (qmltestrunner and the QtTest QML module)" qml_suite_registered
	step "build translator GUI test binaries" build_gui_test_targets
	step "ctest translator GUI suites" \
		ctest --test-dir "$GUI_TEST_DIR" -R '^translator_' --no-tests=error --output-on-failure
}

gui_test_names() {
	ctest --test-dir "$GUI_TEST_DIR" -N -R '^translator_' | sed -n 's/^ *Test *#[0-9]*: *//p'
}

# gui/CMakeLists.txt registers the QML suite only when its tools are installed. Missing
# tools must fail here, or the controller tests would silently stop running.
qml_suite_registered() {
	if gui_test_names | grep -qx "$QML_SUITE"; then
		echo "  $QML_SUITE registered"
		return 0
	fi
	echo "$QML_SUITE is not registered: install qmltestrunner and the QtTest QML module"
	echo "(macOS: brew install qt; Linux: your distribution's Qt 6 QML test runner and QtTest module)"
	return 1
}

# Every translator suite except the QML one is a binary named after its test.
build_gui_test_targets() {
	local targets
	targets="$(gui_test_names | grep -vx "$QML_SUITE" | tr '\n' ' ')"
	if [ -z "$targets" ]; then
		echo "no translator test targets found"
		return 1
	fi
	echo "  targets: $targets"
	# shellcheck disable=SC2086
	cmake --build "$GUI_TEST_DIR" --target $targets
}
