# Offline translation with NLLB-200 + CTranslate2

โหมดออฟไลน์ใช้ **NLLB-200 distilled 600M (INT8)** ผ่าน CTranslate2 โดยไม่ต้องต่ออินเทอร์เน็ต
และไม่ส่งข้อความออกนอกเครื่อง

> **License:** NLLB-200 เป็นโมเดลของ Meta ภายใต้ **CC BY-NC 4.0 (ห้ามใช้เชิงพาณิชย์)**
> จึงไม่ถูก commit หรือบันเดิลใน repo/AppImage นี้ ผู้ใช้ต้องดาวน์โหลดเองและตรวจสอบสัญญาก่อนใช้

## สถานะ

- ✅ C++ backend (`trans::createNllbBackend`) ทำงานจริงและทดสอบแล้ว
- ✅ รองรับ async: งานแปลรันบน worker thread ของ backend ไม่บล็อก OCR pipeline
- ✅ GUI เลือก `Offline NLLB` และกำหนด model directory ได้
- 🟡 ยังไม่ทดสอบบน Steam Deck จริง; memory สูงกว่างบ (ดูด้านล่าง)

## Build

ต้องมี CTranslate2 (ทดสอบกับ 4.9.1), SentencePiece (0.2.3) และ CMake

```bash
# Arch/SteamOS: ติดตั้ง CTranslate2/SentencePiece ตาม distro หรือ build จาก source
cmake -S translator_core -B build-offline -G Ninja \
  -DTRANS_BUILD_TESTS=ON \
  -DTRANS_ENABLE_ONNXRUNTIME=ON \
  -DTRANS_ENABLE_CTRANSLATE2=ON \
  -DCTRANSLATE2_ROOT=/usr \
  -DSENTENCEPIECE_ROOT=/usr
cmake --build build-offline

# Full GUI
cmake -S . -B build -G Ninja \
  -DCHIAKI_ENABLE_TRANSLATOR=ON \
  -DTRANS_ENABLE_ONNXRUNTIME=ON \
  -DTRANS_ENABLE_CTRANSLATE2=ON
cmake --build build --target chiaki -j
```

บน macOS ถ้าใช้ wheel ของ `ctranslate2` จาก pip ให้ชี้ `TRANS_CTRANSLATE2_LIBRARY`
ไปที่ `.../site-packages/ctranslate2/.dylibs/libctranslate2.*.dylib` และแก้ install name
หรือใช้ `install_name_tool` (wheel ถูกสร้างด้วย path `/DLC/...` ซึ่งไม่มีบนเครื่อง)

## โมเดล

```bash
scripts/translator/fetch-nllb.sh
# ดาวน์โหลดไปที่ assets/models/nllb/nllb-200-distilled-600M-ct2-int8
```

หรือแปลงเอง:

```bash
ct2-transformers-converter \
  --model facebook/nllb-200-distilled-600M \
  --quantization int8 \
  --output_dir nllb-200-distilled-600M-ct2-int8
```

ต้องมีไฟล์ `model.bin`, `sentencepiece.bpe.model`, `shared_vocabulary.txt`, `config.json`

## GUI

1. เปิด overlay → Backend → `Offline NLLB`
2. ใส่ `NLLB model dir` (โฟลเดอร์ที่มีไฟล์ข้างต้น) แล้วกด Save
3. ระบบจะโหลดโมเดล (~0.4 วิ บน M1; บน Deck อาจ 1–2 วิ) และเริ่มแปล

## Protocol ที่ใช้ (ทดสอบแล้ว)

NLLB ต้องมี token ภาษาต้นทางและ EOS:

```text
source tokens = ["jpn_Jpan"] + sentencepiece(text) + ["</s>"]
target_prefix = ["tha_Thai"]
hypothesis    = result[0].hypotheses[0][1:]   # ตัด target language token
text          = sentencepiece.decode(hypothesis)
```

การไม่ใส่ `</s>` ทำให้ผลลัพธ์เพี้ยน (ทดลองแล้ว) จึงต้องเติมเสมอ

## ผลวัดบน host (Apple M1, ไม่ใช่ Steam Deck)

| รายการ | ค่า |
| --- | --- |
| โหลดโมเดล | ~0.4–0.5 s |
| แปล 1 บรรทัด | **~0.27–0.43 s** (beam 2, intra_threads 2) |
| Peak RSS | **~1.23 GB** (สูงกว่างบ 850 MB) |
| ตัวอย่าง | `ゲームを続けよう` → `เราก็ยังเล่นกันต่อไป` |
| ตัวอย่าง | `昨日は食べた` → `ฉันกินเมื่อวาน` |

บน Steam Deck คาดว่า latency ~0.8–1.5 s และ RAM ใกล้เคียง 1.2 GB
ถ้า RAM ตึงให้ลด `intra_threads`/`beam_size=1` และปิดแอปอื่น; โมเดล 600M เป็นรุ่นเล็กสุดของ NLLB แล้ว

## ทางเลือกถ้า RAM ไม่พอ

- ใช้ Gemini/DeepL (RAM ~30 MB)
- ใช้โมเดลแปลเล็กกว่า (ต้องแปลงเป็น CT2 เอง) เช่น OPUS-MT ถ้ามีคู่ภาษาญี่ปุ่น–ไทย
- ทำ model offload / โหลดเฉพาะตอนกดแปล แล้ว unload (ยังไม่ implement)
