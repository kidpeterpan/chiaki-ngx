# translator_core

Qt-free C++17 library behind the subtitle translator. Build standalone for fast
iteration:

```bash
cmake -S . -B build -G Ninja -DTRANS_BUILD_TESTS=ON -DTRANS_ENABLE_ONNXRUNTIME=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Public headers are in `include/trans/`:

| Header | Responsibility |
| --- | --- |
| `Types.hpp` | ROI/frame/image/value types shared by every component |
| `FrameProcessor.hpp` | YUV/NV12/P010→RGB, ROI copy, diff, 250 ms stabilization |
| `OcrPostprocess.hpp` | DB box decoding, CTC decoding, crop/rotate, vertical columns |
| `OcrEngine.hpp` | `OcrEngine` interface + ONNX/null factories |
| `TextProcessor.hpp` | UTF-8, normalization, Levenshtein, dedup cache, context, prompts |
| `DictLookup.hpp` | JMdict SQLite lookup, Japanese tokenizer, deinflection |
| `TranslatorBackend.hpp` | Async translator interface + LRU translation cache |
| `NllbBackend.hpp` | Optional offline NLLB-200 via CTranslate2/SentencePiece |
| `TranslatorPipeline.hpp` | Worker thread, OCR→dedup→cache→backend orchestration |
| `RingBuffer.hpp` | Bounded SPSC queue for decoded ROI frames |

Tools:

- `translator_ocr_probe <det.onnx> <rec.onnx> <dict.txt> <image.ppm>`
- `translator_dict_probe <jmdict.db> exact <word>` / `at <text> <byte-offset>`
- `translator_nllb_probe <model_dir> <spm> <src> <tgt> <text>` (with `TRANS_ENABLE_CTRANSLATE2=ON`)
- `translator_onnx_tests` (integration; requires `assets/models`)

License: AGPL-3.0-only-OpenSSL, matching chiaki-ngx. Bundled models and JMdict
have separate licenses (see `assets/models/README.md`, `assets/dict/README.md`).
