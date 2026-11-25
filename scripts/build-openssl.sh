#!/bin/bash
# Builds a static OpenSSL 3 into <dir>/openssl-prefix for the AppImage.
#
# The Ubuntu 20.04 builder only ships OpenSSL 1.1.1, which curl 8.18 and later no longer
# support. A private prefix leaves the image's system OpenSSL untouched; build-appimage.sh
# points curl and chiaki-lib at it. -fPIC because the objects are linked into a PIE.
#
#   scripts/build-openssl.sh <dir>

cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
cd "./$1" || exit 1
ROOT="$(pwd)"

VERSION=3.5.9
SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
NAME="openssl-${VERSION}"
PREFIX="${ROOT}/openssl-prefix"

curl -fsSL -o "${NAME}.tar.gz" \
	"https://github.com/openssl/openssl/releases/download/${NAME}/${NAME}.tar.gz" || exit 1
echo "${SHA256}  ${NAME}.tar.gz" | sha256sum -c - || exit 1
rm -rf "${NAME}"
tar -xzf "${NAME}.tar.gz" || exit 1
cd "${NAME}" || exit 1
./Configure no-shared no-tests no-docs -fPIC --prefix="${PREFIX}" --libdir=lib || exit 1
make -j"$(nproc)" || exit 1
make install_sw || exit 1
