#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Download the Linux x86_64 ONNX Runtime shared library from PyPI and pair it
# with the vendored 1.32 headers. Intended for CI and for building the AppImage
# on a glibc-2.28+ distro.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${1:-$ROOT/.tools/ort-linux}"
VERSION="${ONNXRUNTIME_VERSION:-1.32.0}"
mkdir -p "$PREFIX/include" "$PREFIX/lib"

python3 - "$VERSION" "$PREFIX" <<'PY'
import json, os, shutil, subprocess, sys, tempfile, urllib.request, zipfile
version, prefix = sys.argv[1], sys.argv[2]
data = json.load(urllib.request.urlopen(f"https://pypi.org/pypi/onnxruntime/{version}/json"))
url = None
for item in data["urls"]:
    name = item["filename"]
    if "manylinux" in name and "x86_64" in name and "cp311" in name:
        url = item["url"]
        break
if not url:
    raise SystemExit("no manylinux x86_64 cp311 onnxruntime wheel found")
with tempfile.TemporaryDirectory() as tmp:
    wheel = os.path.join(tmp, "onnxruntime.whl")
    print("downloading", url)
    urllib.request.urlretrieve(url, wheel)
    with zipfile.ZipFile(wheel) as archive:
        for member in archive.namelist():
            if os.path.basename(member) == "libonnxruntime.so.1.32.0":
                with archive.open(member) as source, open(os.path.join(prefix, "lib", "libonnxruntime.so.1.32.0"), "wb") as target:
                    shutil.copyfileobj(source, target)
                break
    lib = os.path.join(prefix, "lib", "libonnxruntime.so.1.32.0")
    if not os.path.exists(lib):
        raise SystemExit("libonnxruntime.so.1.32.0 missing from wheel")
    os.symlink("libonnxruntime.so.1.32.0", os.path.join(prefix, "lib", "libonnxruntime.so"))
    os.symlink("libonnxruntime.so.1.32.0", os.path.join(prefix, "lib", "libonnxruntime.so.1"))
PY

rm -rf "$PREFIX/include/onnxruntime"
cp -R "$ROOT/translator_core/third_party/onnxruntime/include/onnxruntime" "$PREFIX/include/"
echo "ONNX Runtime prefix ready: $PREFIX"
