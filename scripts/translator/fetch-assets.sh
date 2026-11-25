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
	local temp="${output}.part"
	rm -f "$temp"
	curl -fL --retry 5 --retry-all-errors --retry-delay 5 \
		--connect-timeout 30 --max-time 900 -o "$temp" "$url"
	mv "$temp" "$output"
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

# Optional second OCR provider: RapidOCR PP-OCRv5 (about 21 MB).
if [[ "${TRANS_FETCH_RAPIDOCR:-1}" != "0" ]]; then
	"$ROOT/scripts/translator/fetch-rapidocr.sh"
fi

echo
echo "done. Set CHIAKI_TRANSLATOR_ASSETS=$ROOT/assets when running from the source tree."
