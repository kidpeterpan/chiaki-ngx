# shellcheck shell=bash
# Pillar 4 (translator_core): deterministic unit tests. No network: the tests use
# fixtures and the tracked sample dictionary. No test count means failure, so an empty
# or misconfigured suite cannot pass.

stage_test_core() {
	step "ctest translator_core" ctest --test-dir build-core --no-tests=error --output-on-failure
}
