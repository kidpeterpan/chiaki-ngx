# chiaki-ng Translator

ส่วนเสริมของ chiaki-ng สำหรับแปลซับไตเติลเกม PlayStation 5 แบบเรียลไทม์บน Steam Deck
ทำงานในเครื่อง (on-device OCR) แล้วเลือกแปลผ่าน Gemini/DeepL หรือเอาต์พุตข้อความไปใช้ต่อ

> สถานะ: โมดูล `translator_core` และตัวเชื่อม Qt/QML ถูกเขียนและทดสอบแกนหลักครบแล้ว
> (63 unit tests + 2 integration tests ผ่าน, OCR จริงด้วย ONNX Runtime + PP-OCR,
> และคอมไพล์ GUI ทั้งโปรเจกต์ผ่านบน macOS) ส่วนการรันบน Steam Deck จริงและการแพ็ก
> AppImage/Flatpak ต้องทำในรอบถัดไปตาม roadmap

## ทำอะไรได้แล้ว

- ดึงเฉพาะ ROI จากเฟรมวิดีโอที่ถอดรหัสแล้ว (NV12 / I420 / P010 / RGBA / BGRA / GRAY8)
- จำกัดการดึงภาพ 10 Hz และตรวจความนิ่งของข้อความ 250 ms ก่อนสั่ง OCR
- OCR ภาษาญี่ปุ่นด้วย PaddleOCR PP-OCRv3 (ONNX Runtime C++) รองรับข้อความแนวตั้งแบบคอลัมน์
- กรองข้อความซ้ำด้วย Levenshtein similarity ≥ 90%
- แปลแบบมีบริบท 3 ประโยคล่าสุด ผ่าน Gemini Flash หรือ DeepL (Qt Network, async)
- แคชคำแปล LRU, backlog บทสนทนา, และแตะคำเพื่อเปิด JMdict (SQLite)
- Overlay ภายใน viewport เดียวกับวิดีโอ (ไม่เปิดหน้าต่างแยก จึงไม่หลุด focus ใน Gamescope)
- ปุ่มหลังเครื่อง L4/R4/L5/R5 ผ่าน SDL paddle events + คีย์ลัด F13/F14/F15

## โครงสร้าง

```text
translator_core/            C++17 static library (ไม่พึ่ง Qt)
├── include/trans/          API สาธารณะ
├── src/                    FrameProcessor, OCR, pipeline, dict, backends
│   └── NllbBackend.cpp     optional CTranslate2 offline backend
├── tests/                  63 unit tests + 2 ONNX integration tests (CTest)
└── tools/                  build_jmdict.py, ocr_probe, dict_probe, nllb_probe

gui/
├── include/translator/     TranslatorManager (QML singleton), cloud backends
├── src/translator/         Qt adapter + Gemini/DeepL
└── src/qml/translator/     TranslatorOverlay.qml (subtitle, ROI, backlog, settings)

assets/
├── models/                 PP-OCRv3 det + Japan rec + dictionary (Apache-2.0)
└── dict/                   JMdict schema, sample DB, build instructions

packaging/                  AppImageBuilder.yml, Flatpak manifest, AppRun
scripts/translator/         fetch-assets, deploy, build-appimage, Dockerfile
docs/translator/            สถาปัตยกรรม, roadmap, performance, risks
```

## Build & test (core)

ต้องใช้ CMake ≥ 3.16, C++17, SQLite3 และ (ถ้าต้องการ OCR) ONNX Runtime

```bash
# macOS (Homebrew)
brew install cmake ninja sqlite onnxruntime
cmake -S translator_core -B build-core -G Ninja \
  -DTRANS_BUILD_TESTS=ON \
  -DTRANS_ENABLE_ONNXRUNTIME=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure

# Arch / SteamOS
sudo pacman -S --needed cmake ninja sqlite onnxruntime   # onnxruntime อาจต้องใช้ AUR
cmake -S translator_core -B build-core -G Ninja -DTRANS_BUILD_TESTS=ON
cmake --build build-core && ctest --test-dir build-core
```

ทดสอบ OCR กับภาพจริง:

```bash
# สร้างภาพทดสอบหรือใช้ภาพจากเกม แล้วรัน
./build-core/translator_ocr_probe \
  assets/models/ch_PP-OCRv3_det.onnx \
  assets/models/japan_PP-OCRv3_rec.onnx \
  assets/models/japan_dict.txt \
  /path/to/subtitle.ppm
```

ทดสอบพจนานุกรม:

```bash
python3 translator_core/tools/build_jmdict.py \
  --input JMdict_e.gz --output assets/dict/jmdict.db
./build-core/translator_dict_probe assets/dict/jmdict.db at "昨日は食べた" 9
```

## Build ทั้งโปรเจกต์

macOS: ใช้ [`build-macos.sh`](../../scripts/translator/build-macos.sh) พร้อมคู่มือ [MACOS.md](MACOS.md) (มีรายละเอียดใน [MACOS.md](MACOS.md))

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHIAKI_ENABLE_TRANSLATOR=ON \
  -DTRANS_ENABLE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_ROOT=/usr
# optional offline translation:
#  -DTRANS_ENABLE_CTRANSLATE2=ON -DCTRANSLATE2_ROOT=/usr -DSENTENCEPIECE_ROOT=/usr
cmake --build build -j
```

รันจาก source tree:

```bash
CHIAKI_TRANSLATOR_ASSETS=$PWD/assets ./build/gui/chiaki
```

> `assets/dict/jmdict.db` (83 MB) ไม่ได้ commit ไว้ ใช้ `scripts/translator/fetch-assets.sh`
> เพื่อดาวน์โหลดโมเดลและสร้างฐานข้อมูลก่อนแพ็ก

## วิธีใช้บน Steam Deck

1. เปิดเกมผ่าน Remote Play ตามปกติ
2. กดไอคอน **訳** มุมขวาบน เปิด translator แล้วใส่ API key (Gemini หรือ DeepL)
3. เลือก **Select ROI** แล้วลากกรอบบริเวณกล่องข้อความ (แตะ/ลากด้วยจอสัมผัสได้)
   - บันทึกเป็น preset แยกตามเกมได้
4. ระหว่างเล่น
   - **L4** แปลทันที (manual trigger)
   - **R4** ซ่อน/แสดงคำแปล
   - **L5** เปิด backlog
   - **R5** ปรับ ROI
   - สำรอง: **F13/F14/F15** สำหรับผู้ที่ผูกปุ่มผ่าน Steam Input เป็นคีย์บอร์ด

หาก Steam Input ไม่ส่ง paddle event มาเป็น `SDL_CONTROLLER_BUTTON_PADDLE1..4`
ให้ผูก L4/R4/L5 กับ F13/F14/F15 ใน Steam Input แทน (overlay รองรับทั้งสองทาง)

## ข้อจำกัดที่ทราบ

- ข้อความแนวตั้ง: ตัวตรวจจับของ PP-OCRv3 ออกแบบมาสำหรับบรรทัดแนวนอน
  ระบบจึงรวมกล่องตัวอักษรที่ตรวจพบเป็นคอลัมน์ภายหลัง (`mergeVerticalLines`)
  ภาพแนวตั้งที่ฟอนต์ห่างมากอาจตรวจจับไม่ครบ; สามารถเลือก Manga-OCR ในอนาคต
- HDR/zero-copy: ถ้า decoder ส่ง VAAPI/Vulkan frame โดยไม่ transfer ลง software
  ระบบจะ transfer เฉพาะตอนจะ OCR (10 Hz) ซึ่งอาจล้มเหลวบนไดรเวอร์บางตัว;
  ตั้งค่าใช้ software decoding ชั่วคราวได้ถ้าพบปัญหา
- DeepL API บางแผนไม่รองรับภาษาไทย ให้ใช้ Gemini หรือ NLLB แทน
- Offline NLLB เป็น optional build (`-DTRANS_ENABLE_CTRANSLATE2=ON`) และไม่บันเดิลโมเดลเพราะลิขสิทธิ์ CC BY-NC 4.0; RAM ~1.2 GB (สูงกว่างบ)
- API key เก็บใน QSettings แบบ plain text; ควรใช้บัญชี API แยกและจำกัด quota

## Licenses

- โค้ดส่วนนี้: AGPL-3.0-only-OpenSSL ตาม chiaki-ng (ไฟล์ใหม่ใช้ SPDX เดียวกัน)
- PaddleOCR models/dictionary: Apache-2.0
- JMdict: CC BY-SA 4.0 (EDRDG) — ฐานข้อมูลที่แปลงแล้วเป็น derivative work ภายใต้สัญญาเดียวกัน
