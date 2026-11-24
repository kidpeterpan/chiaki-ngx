#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
# Download the optional offline NLLB-200 CTranslate2 model (~600 MB).
#
# The model is NOT distributed with chiaki-ng-translator. NLLB-200 is released
# by Meta under CC BY-NC 4.0 (non-commercial). Review that license before use,
# especially before any commercial distribution.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET="${1:-$ROOT/assets/models/nllb/nllb-200-distilled-600M-ct2-int8}"
BASE="https://huggingface.co/JustFrederik/nllb-200-distilled-600M-ct2-int8/resolve/main"
mkdir -p "$TARGET"

for file in config.json sentencepiece.bpe.model shared_vocabulary.txt model.bin; do
	if [[ -s "$TARGET/$file" ]]; then
		echo "already present: $TARGET/$file"
		continue
	fi
	echo "downloading $file (model.bin is about 594 MB)"
	curl -fL --retry 3 --retry-delay 2 -o "$TARGET/$file" "$BASE/$file"
done

cat > "$TARGET/LICENSE-NOTICE.txt" <<'EOF'
NLLB-200 (facebook/nllb-200-distilled-600M) is copyright Meta Platforms, Inc.
and is distributed under the Creative Commons Attribution-NonCommercial 4.0
International license (CC BY-NC 4.0).
https://creativecommons.org/licenses/by-nc/4.0/
The CTranslate2 conversion here was produced by the model repository author.
This model is not part of chiaki-ng-translator and must not be redistributed as
part of a commercial package without separate permission.
EOF

echo
echo "NLLB model ready: $TARGET"
echo "Configure with -DTRANS_ENABLE_CTRANSLATE2=ON and select Offline NLLB in the overlay."
