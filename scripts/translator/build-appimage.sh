#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Build a single-file AppImage on a SteamOS/Arch-like host.
#
# Requirements: cmake, ninja, Qt6, ffmpeg, libplacebo, sdl2, opus, sqlite,
# ONNX Runtime (or point ONNXRUNTIME_ROOT at a prefix), plus linuxdeploy and
# appimagetool on PATH.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/build-appimage"
APPDIR="$ROOT/AppDir"
OUT="$ROOT/chiaki-ngx-translator-x86_64.AppImage"
ONNXRUNTIME_ROOT="${ONNXRUNTIME_ROOT:-/usr}"

command -v linuxdeploy >/dev/null || { echo "linuxdeploy not found on PATH" >&2; exit 1; }
command -v appimagetool >/dev/null || { echo "appimagetool not found on PATH" >&2; exit 1; }
[[ -f "$ROOT/assets/dict/jmdict.db" ]] || {
	echo "assets/dict/jmdict.db missing; run scripts/translator/fetch-assets.sh" >&2
	exit 1
}

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DCHIAKI_ENABLE_TRANSLATOR=ON \
	-DTRANS_ENABLE_ONNXRUNTIME=ON \
	-DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT" \
	-DCHIAKI_ENABLE_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"

mkdir -p "$APPDIR/usr/share/chiaki-ngx/translator/models" "$APPDIR/usr/share/chiaki-ngx/translator/dict"
cp "$ROOT"/assets/models/*.onnx "$ROOT/assets/models/japan_dict.txt" \
	"$APPDIR/usr/share/chiaki-ngx/translator/models/"
cp "$ROOT/assets/dict/jmdict.db" "$APPDIR/usr/share/chiaki-ngx/translator/dict/"

cp "$ROOT/packaging/appimage/AppRun" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
cp "$ROOT/packaging/chiaki-ngx-translator.desktop" "$APPDIR/usr/share/applications/"

linuxdeploy --appdir "$APPDIR" \
	--desktop-file "$APPDIR/usr/share/applications/chiaki-ngx-translator.desktop" \
	--icon-file "$ROOT/gui/chiaking.png" \
	--plugin qt --output appimage

[[ -f "$OUT" ]] || rm -f ./*.AppImage
ls -lh ./*.AppImage 2>/dev/null || true
echo "AppImage written to $OUT or the current directory."
