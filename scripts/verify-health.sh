#!/usr/bin/env bash
# Health harness for chiaki-ngx. Exit 0 only if every requested check passes.
#
#   scripts/verify-health.sh [static|core|unit|all]...
#
#   static  shell + YAML syntax, submodules initialized, no stray "chiaki-ng" names
#   core    translator_core: configure, build, ctest (Qt-free, standalone)
#   unit    lib/ munit tests (chiaki-unit), GUI, CLI and translator disabled
#
# Default: static core. Env: TRANS_ONNX=ON|OFF (default ON; OFF skips the ONNX tests).
# A missing prerequisite is a FAIL, not a skip. A step that cannot run because an
# earlier step failed is reported as SKIP and also makes the run fail.
# Build trees: build-core/, build-unit/ and build-verify/ at the repo root (gitignored).
# SC2329: check_* functions are invoked indirectly through run().
# shellcheck disable=SC2329
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

# ---- argument and environment validation (before anything runs) ----------------

usage() { echo "usage: $0 [static|core|unit|all]..." >&2; exit 64; }

REQUESTED=()
for arg in "$@"; do
	case "$arg" in
		all) REQUESTED+=(static core unit) ;;
		static|core|unit) REQUESTED+=("$arg") ;;
		*) echo "unknown stage: $arg" >&2; usage ;;
	esac
done
[ ${#REQUESTED[@]} -eq 0 ] && REQUESTED=(static core)

TRANS_ONNX="${TRANS_ONNX:-ON}"
case "$TRANS_ONNX" in
	ON|OFF) ;;
	*) echo "TRANS_ONNX must be ON or OFF (got: $TRANS_ONNX)" >&2; exit 64 ;;
esac

# ---- harness plumbing -----------------------------------------------------------

SUMMARY=()
FAILED=0

# Runs one step and records PASS or FAIL. Returns the step's status so callers can
# skip dependent steps.
run() {
	local name="$1"; shift
	echo
	echo "=== $name"
	"$@"
	local rc=$?
	if [ $rc -eq 0 ]; then
		SUMMARY+=("PASS  $name")
	else
		SUMMARY+=("FAIL  $name (exit $rc)")
		FAILED=1
	fi
	return $rc
}

# Records a step that did not run because an earlier step in its chain failed.
skip() {
	SUMMARY+=("SKIP  $1 (earlier step failed)")
	FAILED=1
}

# ---- private Python venv (YAML check and nanopb generator) ----------------------
# Kept under build-verify/ so the system Python is never modified.
VENV="$ROOT/build-verify/venv"

setup_venv() {
	if "$VENV/bin/python" -c 'import yaml, google.protobuf, grpc_tools' 2>/dev/null; then
		return 0
	fi
	python3 -m venv "$VENV" && "$VENV/bin/pip" install -q pyyaml protobuf grpcio-tools
}

# ---- static ------------------------------------------------------------------------

check_shell_syntax() {
	local bad=0 f
	while IFS= read -r f; do
		bash -n "$f" || { echo "syntax error: $f"; bad=1; }
	done < <(git ls-files -co --exclude-standard -- '*.sh')
	return "$bad"
}

check_yaml_syntax() {
	local bad=0 f
	while IFS= read -r f; do
		"$VENV/bin/python" -c 'import sys, yaml; yaml.safe_load(open(sys.argv[1]))' "$f" 2>/dev/null \
			|| { echo "YAML parse error: $f"; bad=1; }
	done < <(git ls-files -co --exclude-standard -- '.github/*.yml' '.github/*.yaml' 'packaging/*.yml' 'scripts/flatpak/*.y*ml')
	return "$bad"
}

check_submodules() {
	local uninit
	uninit="$(git submodule status | grep '^-' || true)"
	if [ -n "$uninit" ]; then
		echo "uninitialized submodules (run: git submodule update --init --recursive):"
		echo "$uninit"
		return 1
	fi
}

# The fork is named chiaki-ngx. Upstream URLs, external package/image IDs, the Flathub ID
# and release history legitimately keep "chiaki-ng"; anything else is a missed rename.
check_names() {
	# Owner/repo forms come first so the repo segment is consumed together with its owner.
	# share/chiaki-ng/translator is the deliberate pre-rename fallback in translatormanager.cpp.
	local allow='(?:github\.com[:/]|githubusercontent\.com/)chiaki-ng/chiaki-ng|chiaki-ng\.github\.io/chiaki-ng|chiaki-ng/chiaki-ng|chiaki-ng\.github\.io|chiaki-ng\.com|streetpea/chiaki-ng-builder|StreetPea\.chiaki-ng|streetpea/streetpea/chiaki-ng|chiaki-ngImages|setDesktopFileName\("chiaki-ng"\)|io\.github\.streetpea\.chiaki-ng|share/chiaki-ng/translator'
	local hits
	hits="$(git grep --untracked -n 'chiaki-ng' -- . \
		':!doc/release_notes.md' ':!docs/updates/releases.md' \
		':!docs/translator/PERFORMANCE.md' ':!docs/translator/PRIOR-ART.md' \
		':!gui/io.github.streetpea.Chiaki4deck.appdata.xml' ':!scripts/chiaki-ng.rb' \
		':!scripts/verify-health.sh' \
		| ALLOW="$allow" perl -ne 'my $l = $_; $l =~ s/(?:$ENV{ALLOW})//g; print $_ if $l =~ /chiaki-ng(?!x)/')"
	if [ -n "$hits" ]; then
		echo "stray 'chiaki-ng' (should be chiaki-ngx):"
		echo "$hits"
		return 1
	fi
}

stage_static() {
	run "venv for YAML check" setup_venv
	run "shell syntax (bash -n)" check_shell_syntax
	run "YAML syntax" check_yaml_syntax
	run "submodules initialized" check_submodules
	run "no stray chiaki-ng names" check_names
}

# ---- core: translator_core (Qt-free) ----------------------------------------------

stage_core() {
	run "translator_core configure" cmake -S translator_core -B build-core -G Ninja \
		-DTRANS_BUILD_TESTS=ON -DTRANS_ENABLE_ONNXRUNTIME="$TRANS_ONNX" \
		|| { skip "translator_core build"; skip "translator_core ctest"; return; }
	run "translator_core build" cmake --build build-core \
		|| { skip "translator_core ctest"; return; }
	run "translator_core ctest" ctest --test-dir build-core --no-tests=error --output-on-failure
}

# ---- unit: lib/ munit tests ---------------------------------------------------------

# Homebrew's OpenSSL workarounds, mirrored from scripts/translator/build-macos.sh:
#  - bundled curl needs OpenSSL 3 headers and libcrypto (its ENGINE API is gone in 4);
#  - lib/src/aia_openssl.c needs OpenSSL 4's libcrypto for OPENSSL_sk_set_*_thunks.
# Paths are passed explicitly because a build dir configured earlier would otherwise
# keep the OpenSSL 4 paths in its CMake cache. Empty on non-Homebrew machines.
openssl_args() {
	command -v brew >/dev/null 2>&1 || return 0
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

stage_unit() {
	run "venv for nanopb generator" setup_venv \
		|| { skip "chiaki-lib configure"; skip "chiaki-unit build"; skip "chiaki-unit ctest"; return; }

	# Steam Deck native (HIDAPI/FFTW) and RUDP need extra libraries and are not part of
	# the lib/ unit tests, so they are off here.
	local ssl_args=()
	while IFS= read -r line; do
		[ -n "$line" ] && ssl_args+=("$line")
	done < <(openssl_args)

	run "chiaki-lib configure (GUI/CLI/translator off)" cmake -S . -B build-unit -G Ninja \
		-DCMAKE_BUILD_TYPE=Release -DCHIAKI_ENABLE_TESTS=ON \
		-DCHIAKI_ENABLE_GUI=OFF -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_TRANSLATOR=OFF \
		-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_SETSU=OFF -DCHIAKI_ENABLE_RUDP=OFF \
		-DPYTHON_EXECUTABLE="$VENV/bin/python" -DPython3_EXECUTABLE="$VENV/bin/python" \
		${ssl_args[@]+"${ssl_args[@]}"} \
		|| { skip "chiaki-unit build"; skip "chiaki-unit ctest"; return; }
	run "chiaki-unit build" cmake --build build-unit --target chiaki-unit \
		|| { skip "chiaki-unit ctest"; return; }
	run "chiaki-unit ctest" ctest --test-dir build-unit --no-tests=error --output-on-failure
}

# ---- main -----------------------------------------------------------------------------

for stage in "${REQUESTED[@]}"; do
	case "$stage" in
		static) stage_static ;;
		core)   stage_core ;;
		unit)   stage_unit ;;
	esac
done

echo
echo "=== summary"
printf '%s\n' "${SUMMARY[@]}"
if [ "$FAILED" -eq 0 ]; then
	echo "RESULT: healthy"
else
	echo "RESULT: unhealthy"
fi
exit "$FAILED"
