# macOS

## ดาวน์โหลดสำเร็จรูป (แนะนำ)

มี **DMG สำหรับ Apple Silicon (arm64)** ที่รวม Qt/FFmpeg/libplacebo/ONNX Runtime
และ JMdict เต็มไว้แล้ว (ไม่ต้องติดตั้ง Homebrew):

**[chiaki-ngx-translator-arm64.dmg](https://github.com/kidpeterpan/chiaki-ngx/releases/download/v0.2.5-translator/chiaki-ngx-translator-arm64.dmg)**

- SHA-256: `8aa049a467295b8f0f5382e56347ddc3d6928ca564cdf97869ab1c9f65907993`
- เปิด DMG → ลาก `chiaki-ngx.app` ไป Applications → คลิกขวา → Open ครั้งแรก (ad-hoc signed)
- ต้องใช้ macOS 15+ (build จาก `macos-15` runner) และ Apple Silicon
- แอปมี assets อยู่ที่ `Contents/Resources/translator`

## Build เอง

ซับแปลทำงานบน macOS ได้เช่นเดียวกับ Linux/SteamOS เพราะ `translator_core` เป็น C++17
ล้วน และ frontend ใช้ Qt6 QML เดียวกัน เอกสารนี้คือวิธีทำให้ใช้งานได้จริงบนเครื่อง Mac

## สถานะที่ทดสอบแล้ว (macOS 27 arm64)

- ✅ คอมไพล์ GUI ทั้งโปรเจกต์พร้อม translator (Qt 6.12, libplacebo 7.387, FFmpeg 8, ONNX Runtime 1.32)
- ✅ OCR จริงด้วย PP-OCRv3 ผ่าน ONNX Runtime (`ゲームを続けよう` conf 0.980)
- ✅ NLLB-200 INT8 ผ่าน CTranslate2 (ja→th, ~0.27–0.43 s บน M1)
- ✅ Cloud backend ผ่าน mock HTTP; ยังไม่ได้ยิง Gemini/DeepL จริง
- ⚠️ ยังไม่ได้เล่น Remote Play จริงบน Mac (ต้องมี PS5 + PSN); แต่โค้ด path เดียวกับ Deck
- ⚠️ ปุ่มหลังเครื่อง L4/R4/L5 เป็นของ Steam Deck; บน Mac ใช้ overlay หรือ F13–F15

## ติดตั้ง dependencies

```bash
brew install cmake ninja qt libplacebo ffmpeg onnxruntime miniupnpc json-c \
  openssl@3 opus sdl2-compat
```

> `openssl@3` จำเป็นเพราะ bundled curl ของ chiaki-ngx ยังไม่รองรับ OpenSSL 4 ได้ทั้งหมด
> และบน Homebrew รุ่นใหม่ `aia_openssl.c` อาจถูก compile ด้วย header OpenSSL 4
> จึงต้อง link `libcrypto` 4 เพิ่ม; สคริปต์ด้านล่างจัดการให้อัตโนมัติ

## Build + bundle (คำสั่งเดียว)

```bash
scripts/translator/fetch-assets.sh          # โมเดล OCR + jmdict.db (ถ้ายังไม่มี)
scripts/translator/build-macos.sh           # build + copy assets เข้า .app + ad-hoc sign
open build-macos/gui/chiaki.app
```

สคริปต์จะ:

1. ตรวจ Homebrew formula ที่ต้องใช้
2. สร้าง Python venv สำหรับ protobuf generator ถ้าเครื่องยังไม่มี
3. configure/build `chiaki.app` ด้วย translator + ONNX
4. คัดลอกโมเดล/พจนานุกรมไปที่
   `chiaki.app/Contents/Resources/translator/{models,dict}`
5. ad-hoc codesign (`codesign --force --deep --sign -`)

หากไม่มี `assets/dict/jmdict.db` สคริปต์จะใช้ `jmdict.sample.db` (2,000 entries)
เพื่อให้ทดลองได้ แต่ควรสร้างฐานข้อมูลเต็มก่อนใช้งานจริง:

```bash
curl -O https://www.edrdg.org/pub/Nihongo/JMdict_e.gz
python3 translator_core/tools/build_jmdict.py --input JMdict_e.gz --output assets/dict/jmdict.db
```

## Build ด้วยตัวเอง (ไม่ใช้สคริปต์)

```bash
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF \
  -DCHIAKI_ENABLE_SETSU=OFF -DCHIAKI_ENABLE_RUDP=OFF \
  -DCHIAKI_ENABLE_TRANSLATOR=ON -DTRANS_ENABLE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_ROOT="$(brew --prefix onnxruntime)"
cmake --build build-macos --target chiaki -j"$(sysctl -n hw.ncpu)"
```

ถ้าพบ symbol `_OPENSSL_sk_set_cmp_thunks` ตอน link แสดงว่า header/library
OpenSSL ปนรุ่นกัน ให้ใช้สคริปต์ `build-macos.sh` ซึ่ง link `libcrypto` 4 เพิ่มให้

## วิธีใช้

1. เปิด `chiaki.app` แล้วเชื่อมต่อ PS5 ตามปกติ
2. ในหน้า stream กดไอคอน **訳** มุมขวาบน
3. เปิด **Enabled** แล้วเลือก:
   - **Gemini Flash / DeepL**: ใส่ API key
   - **Offline NLLB**: ใส่โฟลเดอร์ CTranslate2 NLLB (ถ้า build ด้วย `TRANS_ENABLE_CTRANSLATE2=ON`)
4. เลือก **Select ROI** แล้วลากกรอบซับด้วยเมาส์/แทร็กแพด
5. กด **Translate now** หรือ **F13** เพื่อแปลทันที; **F14** ซ่อน/แสดง; **F15** backlog
   (บน Mac F13–F15 อาจต้องกด `fn` หรือตั้ง “Use F1, F2, etc. keys as standard function keys”)
6. แตะ/คลิกที่บรรทัดต้นฉบับเพื่อเปิด JMdict

## ข้อจำกัดบน Mac

- **Steam Deck paddles ไม่มี** — ใช้ overlay/F13–F15 แทน
- **Hardware decode**: FFmpeg มักใช้ VideoToolbox; ตัวแปลงจะ transfer frame เป็น NV12
  ก่อน OCR ที่ 10 Hz (เหมือน VA-API) ถ้า zero-copy แล้ว transfer ไม่ได้ ให้ปิด zero-copy
  หรือใช้ software decoding
- **Distribution**: ไบนารีนี้ link ไลบรารี Homebrew; ถ้าจะแจกจ่ายให้รัน
  `macdeployqt`/`dylibbundler` และ sign ด้วย Developer ID (ยังไม่ได้ทำในรอบนี้)
- **Gatekeeper**: ad-hoc signed app อาจต้อง right-click → Open ครั้งแรก
- **ไมโครโฟน/กล้อง**: ระบบ permission ของ macOS อาจถามตาม upstream
- **Offline NLLB**: ต้อง build CTranslate2/SentencePiece เพิ่ม; ยังไม่ใส่ในสคริปต์เริ่มต้น
  ดู [OFFLINE_NLLB.md](OFFLINE_NLLB.md)

## Troubleshooting

| อาการ | วิธีแก้ |
| --- | --- |
| `Library not loaded: libonnxruntime...` | `brew install onnxruntime`; ตรวจ `ONNXRUNTIME_ROOT` |
| `Failed loading SDL3` (เฉพาะ AppImage) | ใช้ build macOS; ไม่เกี่ยวกับ `.app` |
| QML ไม่ขึ้น overlay | ตรวจว่าเปิดในหน้า stream (ไม่ใช่หน้า host list) และ `Translator.enabled` เปิด |
| OCR error “model files not found” | ตรวจ `chiaki.app/Contents/Resources/translator/models` หรือตั้ง `CHIAKI_TRANSLATOR_ASSETS` |
| พจนานุกรมว่าง | คัดลอก `jmdict.db` เต็ม หรือตั้ง `CHIAKI_TRANSLATOR_ASSETS` |
| OpenSSL link error | ใช้ `scripts/translator/build-macos.sh` |
