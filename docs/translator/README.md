# Verification status (อัปเดต 2026-11-24)

## ผ่านแล้ว

| การทดสอบ | คำสั่ง | ผล |
| --- | --- | --- |
| Core unit tests | `ctest --test-dir build-core` | 63/63 ผ่าน (~1.0 s) |
| ONNX integration | `./build-core/translator_onnx_tests` | 2/2 ผ่าน (OCR จริง + pipeline จริง) |
| Sanitizers | ASan/UBSan และ TSan กับ core tests | 63/63 ผ่าน ไม่พบ leak/UAF/race ใน pipeline |
| QML smoke load | `qml` + stub singleton | overlay instantiate โดยไม่มี QML error |
| OCR ภาพซับ | `translator_ocr_probe ... subtitle.ppm` | `ゲームを続けよう` conf 0.980, ocr_ms 26–29 ms (M1 host) |
| NLLB offline (CT2) | `translator_nllb_probe` | ja→th ถูกต้อง 267–429 ms, peak RSS 1.23 GB (host) |
| Cloud backends (mock) | `translator_cloud_tests` | 5/5 ผ่าน: Gemini, worker-thread dispatch, DeepL, HTTP 400, timeout |
| JMdict เต็ม | `build_jmdict.py` + `dict_probe` | 219,217 entries, 500,240 words, 83 MB; ค้น 食べる/食べた ผ่าน |
| GUI compile (macOS) | `scripts/translator/build-macos.sh` | สำเร็จ + bundle models/dict เข้า `.app` + ad-hoc sign; เปิดแอปและโหลด Main.qml ได้ |
| Linux x86_64 (Arch) | Docker + clang + Qt6/FFmpeg/libplacebo + ONNX | `translator_tests` 63/63, `translator_onnx_tests` 2/2; full GUI build สำเร็จ |
| macOS app bundle | `build-macos.sh` + launch | `chiaki.app` มี assets ใน Resources/translator; QML Main โหลดไม่มี translator error |
| Gemini Vision | mock test | ส่งภาพ + ขอ JSON regions พร้อม `translatedText`; pipeline ใช้คำแปลโดยไม่เรียก backend ซ้ำ |
| Live endpoints | curl (2026-11-25) | Google Translate free ja→th คืนคำแปลจริง; OCR.space `helloworld` อ่าน fixture ได้ `ゲームを続けよう` |
| CI release blocker | GitHub Actions 2026-11-25 | v0.2.4 เริ่ม job ไม่ได้: "recent account payments have failed or your spending limit needs to be increased" |
| Chrome Screen AI | C++ probe | โหลด `libchromescreenai.so` + TFLite, อ่าน `ゲームを続けよう` ได้; Linux x86_64 |
| Cloud OCR | mock tests | Google Cloud Vision + OCR.space parse ผ่าน; ยังไม่ทดสอบ key จริง |
| Google translation | mock tests | Google Translate (free) + Google Cloud Translation ผ่าน mock HTTP; ยังไม่ทดสอบ key จริง |
| RapidOCR PP-OCRv5 | probe | อ่าน `ゲームを続けよう` ได้ conf 1.000, ~29 ms บน M1; เลือกได้ใน QML |
| Hotkeys | Qt shortcut tests | parse/validate/conflict ผ่าน; F13/F14/F15/F12 defaults, combination และ empty-disable |
| Chiaki menu | QML | ปุ่ม 訳 ใน inline + separate stream menu เปิด translator settings |
| Touch routing | v0.2.1 | แตะ 訳 เพื่อเปิดเมนู, grabInput ระหว่างเมนู, แตะคำแปลเพื่อ JMdict; แก้ใน `qmlmainwindow.cpp`/`TranslatorOverlay.qml` |
| Steam Deck AppImage | CI `Release chiaki-ngx translator` v0.2.8 | สำเร็จ; มี RapidOCR PP-OCRv5 + models/dict/ONNX; glibc 2.31 |
| macOS DMG | CI `Build chiaki-ngx macOSX` + full dict | สำเร็จ; mount DMG แล้ว `--help` ทำงาน, ONNX ถูก bundle ใน `Frameworks` |
| Repo health | `scripts/verify.sh --full` | PASS ทุก stage: env/lint/build-core/test-core/build-release/test-unit/smoke/security (77s) |
| CI translator tests | GitHub Actions `Translator tests` | success บน main/feature (58a4e0f) |
| MOC | moc + clang syntax check | ผ่านทั้ง `TranslatorManager` และ `CloudTranslatorBackend` |
| QML lint | `qmllint TranslatorOverlay.qml` | ไม่มี syntax error (มี warning เรื่อง import/singleton ที่ resolve ตอน runtime) |
| App launch | `chiaki --help` (offscreen) | เริ่มทำงานถึง renderer init; ไม่ crash จาก translator |

## ยังไม่ได้ทดสอบ (ต้องมี hardware/credential)

- รันบน Steam Deck จริง: VA-API 1080p60, Gamescope, touch ROI, paddle L4/R4/L5
- Gemini/DeepL live API (ต้องมี key) — ทดสอบกับ mock HTTP แล้ว แต่ยังไม่ยิง API จริง
- AppImage/Flatpak build จริง (AppImage ที่ประกอบใน Arch container ตรวจเนื้อหาได้ แต่ยังไม่ยืนยันรันบน SteamOS; ต้อง build บน glibc เก่ากว่า เช่น upstream Ubuntu builder)
- NLLB บน Steam Deck/การวัด RAM จริง

## วิธีรันซ้ำในเครื่องนี้

```bash
# core + tests
cmake -S translator_core -B build-core -G Ninja \
  -DTRANS_BUILD_TESTS=ON -DTRANS_ENABLE_ONNXRUNTIME=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure

# integration (ต้องมี assets/models)
./build-core/translator_onnx_tests

# GUI (macOS ตัวอย่าง; Linux/SteamOS ใช้ pacman deps)
cmake -S . -B build-gui -G Ninja \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DCHIAKI_ENABLE_TRANSLATOR=ON -DTRANS_ENABLE_ONNXRUNTIME=ON \
  -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_ENABLE_SETSU=OFF
cmake --build build-gui --target chiaki -j
```

## ข้อควรระวังจากการทดสอบ

- บน macOS รุ่นนี้ CMake เลือก OpenSSL 4 ให้ bundled curl (compile ไม่ผ่าน)
  ต้องบังคับ OpenSSL 3 และเพิ่ม `libcrypto` 4 ตอน link เฉพาะเครื่องนี้
  (เป็นปัญหา upstream/สภาพแวดล้อม ไม่ใช่โค้ด translator; SteamOS ใช้ OpenSSL 3 ปกติ)
- `translator_onnx_tests` ใช้ fixture `translator_core/tests/fixtures/subtitle.ppm`
  และโมเดลใน `assets/models`
- แอป offscreen บน macOS สร้าง OpenGL context ไม่ได้; การทดสอบ GUI ต้องมีจอจริง
