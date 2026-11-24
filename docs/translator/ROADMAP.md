# Roadmap 6 สัปดาห์ — สถานะจริง

เริ่ม 2026-11-24 · เป้าหมาย: เล่น PS5 Remote Play บน Steam Deck พร้อมซับแปลไทย
แบบ in-viewport โดยไม่ทำให้เฟรมเรตตก

| สัปดาห์ | งาน | สถานะ | หลักฐาน / หมายเหตุ |
| --- | --- | --- | --- |
| 1 | Build env (Docker/Arch), deploy.sh, baseline 1080p60 VA-API | 🟡 โครงสร้างพร้อม + Linux build ผ่าน | Dockerfile/deploy.sh; Arch x86_64 build GUI ผ่าน; ยังไม่ได้รันบน Deck จริง |
| 2 | Frame tap 10 Hz, ROI selector, text stabilization 250 ms | 🟢 แกนหลักเสร็จ | `FrameProcessor`, `TranslatorManager::submitFrame`, `TranslatorOverlay.qml`; unit tests ผ่าน |
| 3 | ONNX Runtime OCR, worker thread/affinity, Levenshtein dedup | 🟢 OCR จริงผ่าน | PP-OCRv3 + Japan rec ตรวจจับ `ゲームを続けよう` conf 0.98; OCR 26–29 ms บน host |
| 4 | Hybrid translation (Gemini/DeepL), JMdict touch lookup, offline NLLB | 🟡 cloud/dict/NLLB พร้อม; ยังไม่ทดสอบ API จริง/Deck | NLLB ja→th 267–429 ms; cloud mock 4/4 (parse/error/timeout) |
| 5 | In-viewport overlay, L4/R4/L5, AppImage | 🟡 โค้ด/manifest เสร็จ ยังไม่แพ็ก | QML overlay + paddle signal; AppImageBuilder/Flatpak/linuxdeploy script |
| 6 | UX polish, profiling on Deck, release | ⚪ ยังไม่เริ่ม | ต้องมี hardware และ API key สำหรับทดสอบ端-to-end |

## Acceptance criteria

### Phase 1
- [x] CMake option `CHIAKI_ENABLE_TRANSLATOR` และ subdirectory แยก
- [x] Dockerfile Arch + deploy script ผ่าน SSH
- [ ] คอมไพล์และรัน baseline บน SteamOS Gaming Mode
- [ ] ยืนยัน VA-API 1080p60 โดยไม่เพิ่ม dropped frames

### Phase 2
- [x] คัดลอกเฉพาะ ROI, แปลง NV12/I420/P010 → RGB
- [x] จำกัด 10 Hz ก่อนดึงภาพ/transfer
- [x] ตรวจความนิ่ง 250 ms ด้วย pixel-delta + global structure similarity
- [x] ไม่ trigger ซ้ำเมื่อข้อความเดิม (generation + dedup)
- [x] QML ROI selector พร้อม preset ต่อเกม
- [ ] ทดสอบบนจอสัมผัส Deck จริง

### Phase 3
- [x] ONNX Runtime C++ (optional build flag)
- [x] PP-OCRv3 detection + Japan CTC recognition
- [x] Worker thread แยก + CPU affinity config
- [x] Dedup ด้วย Levenshtein ≥ 0.9
- [ ] Manga-OCR (encoder-decoder) เป็น backend ทางเลือก
- [ ] Benchmark บน Zen 2 จริง (เป้า 80–150 ms)

### Phase 4
- [x] `ITranslatorBackend` async interface
- [x] Gemini Flash/DeepL backend พร้อม context 3 ประโยค
- [x] แคช LRU + timeout + error surface
- [x] JMdict SQLite + tokenizer + deinflection + tap lookup
- [x] Offline NLLB-200 600M INT8 ผ่าน CTranslate2 (`TRANS_ENABLE_CTRANSLATE2=ON`, optional model)
- [ ] ทดสอบ API จริงด้วย key และวัด latency/โควตา

### Phase 5
- [x] QML overlay วาดใน viewport เดียว (ไม่เปิดหน้าต่างแยก)
- [x] L4/R4/L5/R5 ผ่าน SDL paddle + F13–F15 สำรอง
- [x] Backlog และตั้งค่าใน overlay
- [x] AppImageBuilder.yml, Flatpak manifest, AppRun, build script
- [x] สร้าง AppImage บน Ubuntu 20.04 builder (glibc 2.31) และปล่อย release v0.1.0-steamdeck; CI smoke ผ่าน
- [ ] Flatpak ยังไม่ build
- [x] `scripts/verify.sh --full` ผ่านทุก stage และ CI Translator tests ผ่าน

## งานถัดไปที่Recommended (เรียงตามความเสี่ยง)

1. **คอมไพล์/รันบน Deck** — ปิด zero-copy ถ้า Vulkan transfer ล้มเหลว; วัด dropped frames
2. **Live API test** — Gemini ก่อน (รองรับไทย), ตรวจ timeout/401/quota
3. **Vertical OCR** — ถ้าต้องการเกมแนววิชวลโนเวล ให้เพิ่ม Manga-OCR backend
4. **Offline NLLB distribution** — benchmark บน Deck, ลด RAM/beam, ตัดสินใจ model pack แยก (CC BY-NC)
5. **Sanitizers (GUI)** — รัน manager/QML ด้วย ASan/TSan; core ผ่านแล้ว
6. **Packaging CI** — GitHub Actions สร้าง AppImage จาก Docker Arch
