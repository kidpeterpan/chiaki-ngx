# OCR models

These files are bundled so the AppImage works offline without a model download:

| File | Source | License |
| --- | --- | --- |
| `ch_PP-OCRv3_det.onnx` | PaddleOCR PP-OCRv3 detection model, converted to ONNX (cycloneboy/ch_PP-OCRv3_det_infer on Hugging Face) | Apache-2.0 |
| `japan_PP-OCRv3_rec.onnx` | PaddleOCR Japan PP-OCRv3 recognition model, converted to ONNX (cycloneboy/japan_PP-OCRv3_rec_infer) | Apache-2.0 |
| `japan_dict.txt` | PaddleOCR `ppocr/utils/dict/japan_dict.txt` | Apache-2.0 |

SHA-256:

```text
3439588c030faea393a54515f51e983d8e155b19a2e8aba7891934c1cf0de526  ch_PP-OCRv3_det.onnx
671ddc58febb2a6ed57bd64d53ef34ee368c2cd79d87f4e2bb6501ae03597f62  japan_PP-OCRv3_rec.onnx
2a0842e81ef31d6a99d811a36fc12c33e37aa9406667ef1bd9b468f9108a66bf  japan_dict.txt
```

The recognizer exposes 4401 classes: one CTC blank, 4399 Japanese characters, and
one space token. Vertical text is handled by keeping each character upright and
reassembling columns (`trans::mergeVerticalLines`).

Manga-OCR (a Japanese OCR model trained on manga, Apache-2.0) is a supported
future alternative but has an encoder-decoder architecture; the current engine
implementation targets the CTC PaddleOCR export.

Re-download with `scripts/translator/fetch-assets.sh`.
