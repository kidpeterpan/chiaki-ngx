#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Download OCR models and build the JMdict SQLite database.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODELS="$ROOT/assets/models"
DICT="$ROOT/assets/dict"
mkdir -p "$MODELS" "$DICT"

download() {
	local url="$1" output="$2"
	if [[ -s "$output" ]]; then
		echo "already present: $output"
		return
	fi
	echo "downloading $url"
	curl -fL --retry 3 --retry-delay 2 -o "$output" "$url"
}

download "https://huggingface.co/cycloneboy/ch_PP-OCRv3_det_infer/resolve/main/model.onnx" \
	"$MODELS/ch_PP-OCRv3_det.onnx"
download "https://huggingface.co/cycloneboy/japan_PP-OCRv3_rec_infer/resolve/main/model.onnx" \
	"$MODELS/japan_PP-OCRv3_rec.onnx"
download "https://huggingface.co/cycloneboy/japan_PP-OCRv3_rec_infer/resolve/main/japan_dict.txt" \
	"$MODELS/japan_dict.txt"

JMdict="/tmp/JMdict_e.gz"
download "https://www.edrdg.org/pub/Nihongo/JMdict_e.gz" "$JMdict"

echo "building $DICT/jmdict.db (about 83 MB) ..."
python3 "$ROOT/translator_core/tools/build_jmdict.py" --input "$JMdict" --output "$DICT/jmdict.db"

echo
echo "done. Set CHIAKI_TRANSLATOR_ASSETS=$ROOT/assets when running from the source tree."
