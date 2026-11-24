# Prior art: LunaTranslator และ chiaki-ng

## LunaTranslator (HIllya51/LunaTranslator)

โปรเจกต์ Python/Windows สำหรับแปลเกมวิชวลโนเวลและเกมญี่ปุ่น มีแนวคิดที่นำมาใช้:

- **Levenshtein normalized similarity** (`NativeUtils.similarity`) ใช้กรองข้อความซ้ำ/เกือบซ้ำ
  → เราใช้แนวคิดเดียวกันด้วย implementation UTF-8 ของตัวเองใน `TextProcessor`
  และตั้ง threshold 0.9 ตามแผน
- **Context window 3 ประโยค** ใน `gptcommon.py` (contextOriginal/contextTranslation)
  → `ConversationContext` + `buildUserPrompt` ของเรามีรูปแบบเดียวกัน
- **พจนานุกรมรายคำ** (jisho/jpdb/mojidict) และการแตะคำ
  → เราใช้ JMdict SQLite + tokenizer/deinflection ในเครื่อง
- **Manga-OCR ผ่าน local HTTP server** (`ocrengines/mangaocr.py`)
  → เราเลือก PP-OCRv3 CTC ที่ฝังใน C++ ได้ง่ายกว่า; Manga-OCR เป็นทางเลือกอนาคต
- **ตัวแปลหลายเจ้า** (Gemini/DeepL/Google ฯลฯ) และ prompt ปรับแต่ง
  → เราเปิด interface เดียวและทำ Gemini/DeepL ใน Qt

สิ่งที่ **ไม่** นำมา: การ hook ข้อความจาก process เกม (Windows-only), UI/WebView,
และระบบ plugin Python — เพราะเป้าหมายคือ Steam Deck/Linux และ Remote Play
ที่ภาพวิดีโอเป็นช่องทางเดียวที่เข้าถึงข้อความได้

## chiaki-ng (chiaki-ng/chiaki-ng)

- fork นี้ใช้ upstream เป็นฐาน; decoder อยู่ที่ `lib/src/ffmpegdecoder.c`
  และ frontend ปัจจุบันเป็น QML (`gui/src/qmlbackend.cpp`) ไม่ใช่ Qt Widgets
  ตามแผนเดิม (ดู `ARCHITECTURE.md`)
- ใช้ประโยชน์จาก:
  - `ChiakiFfmpegFrame` / `AVFrame` ที่พร้อมเพลย์
  - `QmlBackend` singleton `Chiaki` สำหรับ QML
  - `ControllerManager` ที่มี paddle events อยู่แล้ว (เดิมถูก ignore)
  - CMake target `chiaki-lib` และ FFmpeg/libplacebo ที่ upstream ตั้งไว้
- ไม่แก้ `lib/` เลยในรอบนี้; hook อยู่ที่ GUI เพื่อลดความเสี่ยงเวลา Sony เปลี่ยน protocol

## ทางเลือกที่พิจารณาแล้วไม่เลือก

| ทางเลือก | เหตุผลที่ไม่เลือก |
| --- | --- |
| Python subprocess (LunaTranslator-style) | หนัก, startup ช้า, แพ็กบน SteamOS/AppImage ยาก |
| Tesseract | ญี่ปุ่นแนวตั้ง/ฟอนต์เกมแย่กว่า PP-OCR มาก |
| Manga-OCR เป็นค่าเริ่มต้น | encoder-decoder + tokenizer ทำให้ binary/โมเดลใหญ่และช้ากว่า |
| ดึงเฟรมเต็ม 60 Hz ไป OCR | แบนด์วิดท์/CPU เกินงบ; ผิดข้อจำกัด VA-API readback |
| เปิด overlay เป็นหน้าต่างแยก | Gamescope ซ่อน/จอยหลุด focus ตามที่ระบุในแผน |
