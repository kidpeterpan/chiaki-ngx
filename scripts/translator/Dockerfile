# Reproducible build/test environment for the translator core and GUI.
# This is Arch Linux because SteamOS is Arch-based.
FROM archlinux:latest

RUN pacman -Syu --noconfirm --needed \
	base-devel cmake ninja git curl rsync python \
	qt6-base qt6-declarative qt6-svg \
	ffmpeg libplacebo sdl2 opus sqlite \
	vulkan-headers vulkan-icd-loader libxcb xcb-util wayland \
	protobuf abseil-cpp re2

# ONNX Runtime is not packaged in the official Arch repositories. Either mount
# a prefix and pass -DONNXRUNTIME_ROOT=/onnxruntime, or build without OCR:
#   docker build -f scripts/translator/Dockerfile -t chiaki-translator .
#   docker run --rm -v "$PWD:/src" chiaki-translator \
#     bash -lc 'cmake -S /src/translator_core -B /tmp/b -G Ninja \
#       -DTRANS_BUILD_TESTS=ON && cmake --build /tmp/b && /tmp/b/translator_tests'

WORKDIR /src
