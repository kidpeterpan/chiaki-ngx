# shellcheck shell=bash
# Platform differences for the verification harness, in one place. Sourced by lib.sh.
#
# Supported: macOS (Homebrew) and Linux (any distro with the libraries installed; the
# package hints cover pacman, apt and dnf). Everything platform-specific belongs here, so
# a new platform means editing this file and nothing else.

PLATFORM="$(uname -s)"

platform_supported() {
	case "$PLATFORM" in
		Darwin|Linux) return 0 ;;
		*) return 1 ;;
	esac
}

# Tools every run needs, whatever the stages.
platform_base_tools() {
	echo "git python3 perl"
	case "$PLATFORM" in
		Darwin) echo "brew" ;;
		Linux)  echo "pkg-config" ;;
	esac
}

# Tools a given stage needs. Checking only what the requested stages use means a
# contributor running --quick does not have to install the security scanners.
stage_tools() {
	case "$1" in
		lint)       echo "shellcheck actionlint qmllint" ;;
		build-core|test-core|build-release|test-unit|test-gui) echo "cmake ninja" ;;
		security)   echo "gitleaks osv-scanner rsync" ;;
		env|smoke)  echo "" ;;
	esac
}

# Every tool the harness can use, for the version record.
platform_tools() {
	local s out
	out="$(platform_base_tools)"
	for s in $ALL_STAGES; do
		out="$out $(stage_tools "$s")"
	done
	printf '%s\n' $out | sort -u | tr '\n' ' '
}

# Library prerequisites, split by what a stage actually links against. On macOS these are
# Homebrew formula names; on Linux they are pkg-config modules, which is distro-agnostic.
#
# translator_core needs only SQLite (macOS ships it in the SDK, so nothing to install);
# the app needs the full Qt/FFmpeg/libplacebo stack.
platform_core_libs() {
	case "$PLATFORM" in
		Darwin) echo "" ;;
		Linux)  echo "sqlite3" ;;
	esac
}

platform_app_libs() {
	case "$PLATFORM" in
		Darwin) echo "qt libplacebo ffmpeg onnxruntime miniupnpc json-c openssl@3 opus" ;;
		Linux)  echo "opus json-c libplacebo libavcodec libavutil openssl" ;;
	esac
}

stage_libs() {
	case "$1" in
		build-core|test-core) platform_core_libs ;;
		build-release|test-unit|test-gui) platform_app_libs ;;
		*) echo "" ;;
	esac
}

# True when any planned stage configures the app, so SDL and the app stack are required.
plan_needs_app() {
	case " ${VERIFY_PLAN:-} " in
		*" build-release "*|*" test-unit "*|*" test-gui "*) return 0 ;;
		*) return 1 ;;
	esac
}

platform_lib_present() {
	case "$PLATFORM" in
		Darwin) brew list --versions "$1" >/dev/null 2>&1 ;;
		Linux)  pkg-config --exists "$1" ;;
	esac
}

# Printed when something is missing, so the fix is one copy-paste.
platform_install_hint() {
	case "$PLATFORM" in
		Darwin)
			echo "brew install cmake ninja qt libplacebo ffmpeg onnxruntime miniupnpc json-c \\"
			echo "  openssl@3 opus sdl2-compat shellcheck actionlint gitleaks osv-scanner"
			;;
		Linux)
			echo "Arch/SteamOS: pacman -S --needed base-devel cmake ninja sqlite qt6-base \\"
			echo "  qt6-declarative ffmpeg libplacebo sdl2 opus json-c miniupnpc openssl \\"
			echo "  pkgconf shellcheck"
			echo "Debian/Ubuntu: apt install build-essential cmake ninja-build libsqlite3-dev \\"
			echo "  qt6-base-dev qt6-declarative-dev libavcodec-dev libplacebo-dev libsdl2-dev \\"
			echo "  libopus-dev libjson-c-dev libminiupnpc-dev libssl-dev pkg-config shellcheck"
			echo "actionlint, gitleaks and osv-scanner: see each project's release page."
			;;
	esac
}

# Extra SDL check: macOS accepts either formula, Linux resolves it through pkg-config.
platform_sdl_present() {
	case "$PLATFORM" in
		Darwin) brew list --versions sdl2 >/dev/null 2>&1 || brew list --versions sdl2-compat >/dev/null 2>&1 ;;
		Linux)  pkg-config --exists sdl2 ;;
	esac
}

# CMake arguments for the lib/ unit tests.
#
# macOS/Homebrew only: the bundled curl needs OpenSSL 3 (its ENGINE API is gone in 4), while
# lib/src/aia_openssl.c needs OpenSSL 4's OPENSSL_sk_set_*_thunks, so both are linked. See
# TRIAGE T3. Linux distributions ship a single consistent OpenSSL, so nothing is needed.
platform_openssl_args() {
	[ "$PLATFORM" = Darwin ] || return 0
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

# Where the app stores settings, for the smoke check that asserts nothing was written.
platform_settings_paths() {
	case "$PLATFORM" in
		Darwin) printf '%s\n' "$HOME/Library/Preferences" "$HOME/Library/Application Support" ;;
		Linux)  printf '%s\n' "${XDG_CONFIG_HOME:-$HOME/.config}" "${XDG_DATA_HOME:-$HOME/.local/share}" ;;
	esac
}

# stat(1) is not portable: BSD takes -f, GNU takes -c.
platform_stat_fields() {
	case "$PLATFORM" in
		Darwin) stat -f '%N %m %z' "$@" ;;
		Linux)  stat -c '%n %Y %s' "$@" ;;
	esac
}

# The app binary inside the build tree.
platform_app_binary() {
	case "$PLATFORM" in
		Darwin) echo "build-release/gui/chiaki.app/Contents/MacOS/chiaki" ;;
		Linux)  echo "build-release/gui/chiaki" ;;
	esac
}

platform_cpus() {
	case "$PLATFORM" in
		Darwin) sysctl -n hw.ncpu ;;
		Linux)  nproc ;;
	esac
}
