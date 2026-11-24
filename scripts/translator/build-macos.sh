#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Build the translator-enabled chiaki-ng app on macOS and bundle models/dict
# into chiaki.app/Contents/Resources/translator.
#
# Homebrew prerequisites:
#   brew install cmake ninja qt libplacebo ffmpeg onnxruntime miniupnpc json-c \
#     openssl@3 opus sdl2
#
# The local build may need OpenSSL 3 for the bundled curl and OpenSSL 4's
# libcrypto for symbols emitted by lib/src/aia_openssl.c on newer Homebrew.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-macos}"
QT_PREFIX="$(brew --prefix qt)"
OPENSSL3="$(brew --prefix openssl@3)"
OPENSSL4="$(brew --prefix openssl 2>/dev/null || true)"
APP="$BUILD/gui/chiaki.app"

for cmd in cmake ninja brew codesign; do
	command -v "$cmd" >/dev/null || { echo "missing $cmd" >&2; exit 1; }
done
for formula in qt libplacebo ffmpeg onnxruntime miniupnpc json-c openssl@3 opus; do
	brew list --versions "$formula" >/dev/null 2>&1 || {
		echo "missing Homebrew formula: $formula" >&2
		exit 1
	}
done
if ! brew list --versions sdl2 >/dev/null 2>&1 && ! brew list --versions sdl2-compat >/dev/null 2>&1; then
	echo "missing Homebrew formula: sdl2 (or sdl2-compat)" >&2
	exit 1
fi

PYTHON_BIN="$(command -v python3)"
if ! "$PYTHON_BIN" -c 'import google.protobuf' >/dev/null 2>&1; then
	VENV="$BUILD/.python-venv"
	if [[ ! -x "$VENV/bin/python" ]]; then
		"$PYTHON_BIN" -m venv "$VENV"
	fi
	"$VENV/bin/pip" install --quiet --disable-pip-version-check protobuf grpcio-tools
	PYTHON_BIN="$VENV/bin/python"
fi

EXTRA_LIBS=()
if [[ -n "$OPENSSL4" && -f "$OPENSSL4/lib/libcrypto.dylib" ]]; then
	EXTRA_LIBS+=("$OPENSSL4/lib/libcrypto.dylib")
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$QT_PREFIX" \
	-DPYTHON_EXECUTABLE="$PYTHON_BIN" \
	-DPython3_EXECUTABLE="$PYTHON_BIN" \
	-DOPENSSL_ROOT_DIR="$OPENSSL3" \
	-DCMAKE_C_FLAGS="-I$OPENSSL3/include" \
	-DCMAKE_CXX_FLAGS="-I$OPENSSL3/include" \
	-DCMAKE_CXX_STANDARD_LIBRARIES="${EXTRA_LIBS[*]}" \
	-DCHIAKI_ENABLE_CLI=OFF \
	-DCHIAKI_ENABLE_TESTS=OFF \
	-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF \
	-DCHIAKI_ENABLE_SETSU=OFF \
	-DCHIAKI_ENABLE_RUDP=OFF \
	-DCHIAKI_ENABLE_TRANSLATOR=ON \
	-DTRANS_ENABLE_ONNXRUNTIME=ON \
	-DONNXRUNTIME_ROOT="$(brew --prefix onnxruntime)"

cmake --build "$BUILD" --target chiaki -j"$(sysctl -n hw.ncpu)"

ASSETS="$APP/Contents/Resources/translator"
mkdir -p "$ASSETS/models" "$ASSETS/dict"
cp "$ROOT"/assets/models/*.onnx "$ROOT/assets/models/japan_dict.txt" "$ASSETS/models/"
if [[ -f "$ROOT/assets/dict/jmdict.db" ]]; then
	cp "$ROOT/assets/dict/jmdict.db" "$ASSETS/dict/jmdict.db"
else
	echo "warning: full jmdict.db not found; bundling the small sample database" >&2
	cp "$ROOT/assets/dict/jmdict.sample.db" "$ASSETS/dict/jmdict.db"
fi

# Ad-hoc sign after adding resources; replace with a Developer ID identity for
# distribution: codesign --force --deep --sign "Developer ID Application: ..." "$APP"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

echo
echo "Built: $APP"
echo "Run:   open \"$APP\""
echo "Assets: $ASSETS"
