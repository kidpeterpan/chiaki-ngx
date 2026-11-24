#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Cross-deploy from a development PC to a Steam Deck over SSH.
#
#   DECK_HOST=steamdeck scripts/translator/deploy.sh
#   DECK_HOST=deck@192.168.1.42 DECK_DEST='~/Applications/chiaki-ngx-translator' \
#     scripts/translator/deploy.sh --run
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DECK_HOST="${DECK_HOST:-steamdeck}"
DECK_DEST="${DECK_DEST:-~/Applications/chiaki-ngx-translator}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-deck}"
ONNXRUNTIME_ROOT="${ONNXRUNTIME_ROOT:-/usr}"
RUN=0
[[ "${1:-}" == "--run" ]] && RUN=1

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_ENABLE_TRANSLATOR=ON \
	-DTRANS_ENABLE_ONNXRUNTIME=ON \
	-DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT" \
	-DCHIAKI_ENABLE_TESTS=OFF
cmake --build "$BUILD_DIR" -j"$(nproc)"

ssh "$DECK_HOST" "mkdir -p $DECK_DEST"
rsync -az --delete "$BUILD_DIR/gui/chiaki" "$DECK_HOST:$DECK_DEST/"
rsync -az --delete "$ROOT/assets/models/" "$DECK_HOST:$DECK_DEST/models/"
rsync -az "$ROOT/assets/dict/jmdict.db" "$DECK_HOST:$DECK_DEST/"

ssh "$DECK_HOST" "cat > $DECK_DEST/run.sh <<'EOF'
#!/bin/sh
HERE=\$(dirname \"\$(readlink -f \"\$0\")\")
export CHIAKI_TRANSLATOR_ASSETS=\"\$HERE\"
exec \"\$HERE/chiaki\" \"\$@\"
EOF
chmod +x $DECK_DEST/run.sh"

if [[ $RUN -eq 1 ]]; then
	ssh "$DECK_HOST" "pkill -x chiaki 2>/dev/null || true; cd $DECK_DEST && ./run.sh >/tmp/chiaki-ngx-translator.log 2>&1 &"
	echo "started on $DECK_HOST (log: /tmp/chiaki-ngx-translator.log)"
fi
