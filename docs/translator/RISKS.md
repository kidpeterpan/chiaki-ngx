# ความเสี่ยงทางเทคนิคและแนวทางป้องกัน

| # | ความเสี่ยง | ผลกระทบ | แนวทางที่ทำแล้ว / ต่อไป | สถานะ |
| --- | --- | --- | --- | --- |
| 1 | ดึงเฟรมจาก GPU (VA-API) กลับ CPU ทำให้แบนด์วิดท์เต็ม | เฟรมตก | คัดลอกเฉพาะ ROI, 10 Hz, transfer hw→sw เฉพาะเมื่อจะ OCR; ถ้า Vulkan zero-copy โอนไม่ได้ให้ fallback software decode | 🟡 ออกแบบแล้ว ต้องทดสอบ Deck |
| 2 | ฟอนต์เกมประดับ/พื้นหลังโปร่งใส ทำให้ OCR ผิด | แปลผิด | `enhanceContrast` + `adaptiveThreshold`, ROI แคบ, ใช้ PP-OCRv3 ที่ทนพื้นหลัง; อนาคตเพิ่ม OpenCV pipeline | 🟢 มี preprocessing |
| 3 | ข้อความแนวตั้งตรวจไม่ครบ | อ่านขาด | เก็บกล่อง upright + `mergeVerticalLines`; ทางเลือก Manga-OCR | 🟡 บางส่วน |
| 4 | API cloud ช้า/ล่ม/โควตาหมด | ไม่มีคำแปล | timeout 20 s, error ขึ้น overlay, cache LRU, dedup; ต้องเพิ่ม retry/backoff | 🟡 ขาด retry |
| 5 | API key รั่วจาก QSettings | ความปลอดภัย | เก็บ plain text ตามข้อจำกัด; แนะนำ key แยก/จำกัด quota; ไม่ log key; อนาคตใช้ keyring | 🟡 ยอมรับชั่วคราว |
| 6 | Overlay เปิดหน้าต่างแยกแล้ว Gamescope ซ่อน/จอยหลุด focus | เล่นไม่ได้ | QML child ของ StreamView เท่านั้น; settings/backlog วาดใน viewport; ไม่มี popup window | 🟢 |
| 7 | ปุ่มหลังเครื่องไม่ส่ง paddle event (Steam Input intercept) | กดปุ่มไม่ทำงาน | ใช้ SDL PADDLE1–4 ถ้ามี + คีย์ลัด F13–F15 ให้ผูกผ่าน Steam Input | 🟢 มี fallback |
| 8 | Race/UAF ระหว่าง stop กับ callback network | crash | `TranslatorPipeline` ใช้ shared state + join; callback เช็ค `alive_`; ต้องรัน ASan/TSan | 🟡 ต้องตรวจ |
| 9 | NLLB offline กิน RAM ~850 MB | OOM บน Deck | ยังไม่รวมในไบนารี; ทำเป็น optional model pack + CPU affinity 1–2 คอร์ | ⚪ อนาคต |
| 10 | โมเดล/พจนานุกรมมีลิขสิทธิ์ต่างกัน | เผยแพร่ไม่ได้ | ระบุ Apache-2.0 (PaddleOCR), CC BY-SA 4.0 (JMdict) ใน `assets/*/README.md`; ไม่รวม NLLB จนตรวจสัญญา | 🟢 เอกสารแล้ว |
| 11 | AppImage รวม Qt/ONNX แล้วใหญ่ | ดาวน์โหลดช้า | แยก model pack ได้ในอนาคต; ตอนนี้ ~150 MB, ยอมรับได้ | 🟡 |
| 12 | Merge conflict เมื่อ upstream เปลี่ยน QML | maintain ไม่ไหว | แยกโมดูล + 6 hook points + `#ifdef CHIAKI_ENABLE_TRANSLATOR`; มีเอกสาร hook ใน ARCHITECTURE.md | 🟢 |
| 13 | OCR ภาพ 1080p ถูก downscale ที่ limit 960 ทำให้ตัวอักษรเล็กหาย | อ่านไม่ครบ | ปรับ `detection_limit_side` ตาม ROI; ROI แคบช่วยให้ตัวอักษรใหญ่ขึ้นเมื่อเทียบ | 🟡 ต้อง tune |
| 14 | Dedup ผิดพลาดกลืนบทสนทนาที่คล้ายกัน | แปลหาย | threshold 0.9 + TTL 30 s + manual trigger bypass; ปรับได้ผ่าน `DedupConfig` | 🟢 |
