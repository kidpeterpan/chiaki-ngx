#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Reproduce the SteamOS-shaped build on an x86_64 Arch Linux container.
#
# Prerequisites:
#   - docker with linux/amd64 support
#   - an ONNX Runtime Linux prefix at $ORT_PREFIX (include/onnxruntime + lib/libonnxruntime.so)
#
#   ORT_PREFIX=$PWD/.tools/ort-linux scripts/translator/build-linux-container.sh core
#   ORT_PREFIX=$PWD/.tools/ort-linux scripts/translator/build-linux-container.sh gui
#
# The pacman sandbox is disabled because seccomp sandboxing fails under some
# x86_64 emulation layers (e.g. Apple Silicon Rosetta).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ORT_PREFIX="${ORT_PREFIX:-}"
MODE="${1:-core}"
if [[ -z "$ORT_PREFIX" || ! -d "$ORT_PREFIX/include/onnxruntime" ]]; then
	echo "set ORT_PREFIX to a Linux ONNX Runtime prefix" >&2
	exit 1
fi
if [[ "$MODE" == "gui" ]]; then
	BUILD_DIR=".tools/build-gui-linux"
	CMAKE_ARGS=(
		-DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_TESTS=OFF
		-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_SETSU=OFF
		-DCHIAKI_ENABLE_RUDP=OFF
		-DCHIAKI_ENABLE_TRANSLATOR=ON -DTRANS_ENABLE_ONNXRUNTIME=ON
		-DTRANS_ONNXRUNTIME_INCLUDE_DIR=/opt/ort/include/onnxruntime
		-DTRANS_ONNXRUNTIME_LIBRARY=/opt/ort/lib/libonnxruntime.so
		-DCHIAKI_USE_SYSTEM_CURL=ON
	)
	TARGET=chiaki
else
	BUILD_DIR=".tools/build-linux"
	CMAKE_ARGS=(
		-DTRANS_BUILD_TESTS=ON -DTRANS_ENABLE_ONNXRUNTIME=ON
		-DTRANS_ONNXRUNTIME_INCLUDE_DIR=/opt/ort/include/onnxruntime
		-DTRANS_ONNXRUNTIME_LIBRARY=/opt/ort/lib/libonnxruntime.so
	)
	TARGET=""
fi

docker run --rm --platform linux/amd64 \
	-v "$ROOT:/src" -v "$ORT_PREFIX:/opt/ort" -w /src archlinux:latest bash -lc "
set -e
sed -i 's/^#DisableSandboxSyscalls/DisableSandboxSyscalls/' /etc/pacman.conf
pacman -Syu --noconfirm --needed base-devel cmake ninja git sqlite clang llvm python-protobuf >/dev/null
pacman -S --noconfirm --needed qt6-base qt6-declarative qt6-svg ffmpeg libplacebo sdl2 opus \
	vulkan-headers vulkan-icd-loader libxcb xcb-util wayland libxkbcommon openssl json-c \
	miniupnpc libevent libidn2 libpsl libssh2 zlib protobuf abseil-cpp re2 >/dev/null
export CC=clang CXX=clang++
cmake -S . -B $BUILD_DIR -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ${CMAKE_ARGS[*]}
cmake --build $BUILD_DIR ${TARGET:+--target $TARGET} -j2
"
if [[ "$MODE" == "core" ]]; then
	docker run --rm --platform linux/amd64 -v "$ROOT:/src" -v "$ORT_PREFIX:/opt/ort" -w /src \
		archlinux:latest bash -lc 'LD_LIBRARY_PATH=/opt/ort/lib ctest --test-dir .tools/build-linux --output-on-failure'
fi
