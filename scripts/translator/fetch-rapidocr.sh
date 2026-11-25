#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Fetch the RapidOCR PP-OCRv5 models used by the Decky-Translator-compatible
# OCR option. The ch recognition model handles Japanese and mixed CJK/English.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEST="$ROOT/assets/models/rapidocr"
mkdir -p "$DEST"

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

BASE="https://github.com/MeKo-Christian/paddleocr-onnx/releases/download/v1.0.0"
DICT="https://huggingface.co/monkt/paddleocr-onnx/resolve/main/languages/chinese/dict.txt"
download "$BASE/PP-OCRv5_mobile_det.onnx" "$DEST/ch_PP-OCRv5_mobile_det.onnx"
download "$BASE/PP-OCRv5_mobile_rec.onnx" "$DEST/ch_rec.onnx"
download "$DICT" "$DEST/ch_dict.txt"

echo "RapidOCR models ready in $DEST"
