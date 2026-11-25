#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Fetch the Chrome Screen AI package (libchromescreenai.so + TFLite models,
# ~123 MB) for the optional on-device OCR provider.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEST="$ROOT/assets/models/screenai"
VERSION="${SCREENAI_CIPD_VERSION:-b9c06974122103e4dcfe4bbc942c5a90668a1b42df880786bcd86e450fde33ad}"

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
	echo "Chrome Screen AI is Linux x86_64 only; skipping on $(uname -s)/$(uname -m)"
	exit 0
fi
if [[ -f "$DEST/resources/libchromescreenai.so" ]]; then
	echo "already present: $DEST"
	exit 0
fi

python3 - "$VERSION" "$DEST" <<'PY'
import io, json, os, shutil, sys, tempfile, urllib.request, zipfile

version, dest = sys.argv[1], sys.argv[2]
package = "chromium/third_party/screen-ai/linux"
host = "https://chrome-infra-packages.appspot.com"

def post(url, payload):
    request = urllib.request.Request(url, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "Accept": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        body = response.read()
    if body.startswith(b")]}'"):
        body = body[4:]
    return json.loads(body)

if len(version) == 64 and all(c in "0123456789abcdef" for c in version):
    instance = {"hashAlgo": "SHA256", "hexDigest": version}
else:
    instance = post(host + "/prpc/cipd.Repository/ResolveVersion",
        {"package": package, "version": version})["instance"]
url = post(host + "/prpc/cipd.Repository/GetInstanceURL",
    {"package": package, "instance": instance})["signedUrl"]
print("downloading Screen AI package ...")
with urllib.request.urlopen(url, timeout=300) as response:
    archive = response.read()
print(f"{len(archive) / 1024 / 1024:.0f} MB")
os.makedirs(dest, exist_ok=True)
with tempfile.TemporaryDirectory(dir=dest) as temporary:
    with zipfile.ZipFile(io.BytesIO(archive)) as package_zip:
        package_zip.extractall(temporary)
    resources = os.path.join(temporary, "resources")
    if not os.path.exists(os.path.join(resources, "libchromescreenai.so")):
        raise SystemExit("downloaded package has no libchromescreenai.so")
    for name in os.listdir(dest):
        if name == os.path.basename(temporary):
            continue
        path = os.path.join(dest, name)
        shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
    for name in os.listdir(temporary):
        shutil.move(os.path.join(temporary, name), os.path.join(dest, name))
os.chmod(os.path.join(dest, "resources", "libchromescreenai.so"), 0o755)
PY

echo "Chrome Screen AI assets ready in $DEST"
