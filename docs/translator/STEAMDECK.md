# Steam Deck / Linux x86_64 (AppImage)

## ดาวน์โหลด

**[chiaki-ngx-translator-x86_64.AppImage](https://github.com/kidpeterpan/chiaki-ngx/releases/latest/download/chiaki-ngx-translator-x86_64.AppImage)** (release ล่าสุดเสมอ)

SHA-256 อยู่ในไฟล์ `chiaki-ngx-translator-x86_64.AppImage.sha256` ของ release เดียวกัน

ใช้ **v0.1.0** ขึ้นไป: real-time toggle, touch, Chrome Screen AI, Decky-Translator providers และหน้า settings ที่ใช้ controller ได้
**v0.1.1** เพิ่ม: แปล live ได้แม้ฉากหลังกล่องข้อความขยับ, badge LIVE / MANUAL / OFF, ซ่อนข้อความต้นฉบับ, ปรับความทึบกล่อง และ dropdown ที่กด A แล้วเปิดค้างได้
release ชุด v0.2.x เดิมถูกถอนออกแล้ว (ปิดแอปได้เมื่อใช้ Chrome Screen AI แล้วเปลี่ยน settings และ curl ที่ bundle มามี CVE)

## ติดตั้งบน Steam Deck

1. ดาวน์โหลดไปที่โฟลเดอร์ เช่น `~/Downloads`
2. เปิด Konsole แล้วทำ:
   ```bash
   chmod +x ~/Downloads/chiaki-ngx-translator-x86_64.AppImage
   mkdir -p ~/Applications
   mv ~/Downloads/chiaki-ngx-translator-x86_64.AppImage ~/Applications/
   ```
3. เปิด **Steam** → **Games** → **Add a Non-Steam Game** → Browse ไปที่
   `~/Applications/chiaki-ngx-translator-x86_64.AppImage`
4. กลับไปที่ Gaming Mode แล้วเปิดจาก library ได้เลย

ถ้า AppImage ไม่ยอมรันเพราะ FUSE ให้รันด้วย:

```bash
~/Applications/chiaki-ngx-translator-x86_64.AppImage --appimage-extract-and-run
```

หรือเพิ่ม `--appimage-extract-and-run` ใน Launch Options ของ Steam

## Touch บน Steam Deck

ทุกเวอร์ชันตั้งแต่ **v0.1.0**:

- แตะไอคอน **訳** มุมขวาบนได้เลย แม้เกมกำลังใช้ touchscreen เป็น PS touchpad
- เมื่อเมนูเปิด ระบบจะ `grabInput()` ให้ touch ทั้งหมดมาที่ overlay จึงกดปุ่ม/เลือก ROI/เปิด backlog ได้
- แตะที่คำแปลเพื่อเปิด JMdict
- ปิดเมนูแล้ว touch จะกลับไปเป็น PS touchpad ตามเดิม

ถ้าแตะไม่ติด ให้อัปเดตเป็น release ล่าสุด

## Chrome Screen AI

**AppImage มี Chrome Screen AI พร้อม assets ฝังอยู่ในตัวแล้ว** (Linux x86_64)
เลือกได้ที่ช่อง OCR → `Chrome Screen AI (Linux)`; ไม่ต้องดาวน์โหลดเพิ่ม

## เปิด settings จาก Chiaki menu

นอกจากกดไอคอน **訳** มุมขวาบนแล้ว เปิดแผง translator ได้จาก **stream menu ของ Chiaki**
(ปุ่ม `Ctrl+O` หรือปุ่ม stream menu บนจอย) → กดปุ่ม **訳** ในแถบเมนู

## Hotkeys

แก้ได้ในแผง translator → หัวข้อ **Hotkeys**:

- รองรับปุ่มเดี่ยว (`F13`) และ combination (`Ctrl+Shift+T`)
- ปุ่ม **Live** ใน Chiaki stream menu, badge ใต้มุมขวาบน และ `F11` สลับการแปลอัตโนมัติ (live) ได้ทันที
  - badge **LIVE** = แปลเองเมื่อข้อความนิ่ง, **MANUAL** = แปลเฉพาะตอนกด L4 / Translate now, **OFF** = ปิด translator
  - กดตอน LIVE → MANUAL; กดตอน MANUAL หรือ OFF → LIVE (ถ้า translator ปิดอยู่จะเปิดให้ด้วย)
  - ปิด translator ทั้งหมดใช้ปุ่ม **Turn off** ในการ์ด Translator ของหน้า settings
- เว้นว่าง = ปิด shortcut นั้น
- ตรวจ conflict ระหว่าง 4 actions: Translate, Overlay, Backlog, Settings
- บันทึกใน QSettings

ค่าเริ่มต้น: `F11` สลับ LIVE / MANUAL, `F13` แปล, `F14` ซ่อน/แสดง, `F15` backlog, `F12` เปิด settings

## Timing

ใน settings ปรับได้:

- **Hide after (s)** — ให้ overlay แปลหายอัตโนมัติหลัง N วินาที (`0` = ไม่หาย)
- **Scan interval (ms)** — ความถี่ในการตรวจภาพ/ข้อความใหม่ (50–2000 ms)
- **Stable time (ms)** — ข้อความต้องนิ่งนานเท่าไรก่อน OCR/แปล (100–5000 ms)
- **Live translation** (หัวข้อ Setup) — On = แปลอัตโนมัติ, Off = แปลเฉพาะตอนกด L4 / Translate now

"นิ่ง" วัดจาก pixel ที่เป็นตัวอักษร (สว่างมากบนพื้นมืด หรือมืดมากบนพื้นสว่าง) ไม่ใช่ทั้งกรอบ
ฉากที่ขยับอยู่หลังกล่องข้อความโปร่งแสงจึงไม่ทำให้ live ค้าง ถ้ากรอบยังขยับไม่หยุดเกิน 1.5 วินาที
ระบบจะ OCR ช้าๆ ทุก 1.5 วินาที (ถ้า 3 ครั้งติดไม่เจอข้อความใหม่ เช่นไม่มีข้อความหรือเป็นบรรทัดที่แปลแล้ว จะเว้นเป็น 5 วินาที)
และแปลเมื่ออ่านได้ข้อความเดิมสองครั้งติดกัน บรรทัดสถานะจะขึ้น "Live: waiting for the text to settle"
หรือ "Live: reading a moving area" ให้รู้ว่ากำลังรออยู่ตรงไหน
OCR แบบ cloud (Google Vision, Gemini Vision, OCR.space) ไม่มีการอ่านสำรองนี้ เพราะทุกครั้งคือ network call

## Hotkey capture

กด **Set** ข้างช่อง hotkey แล้วกดคีย์ผสมที่ต้องการได้เลย
เช่น `Ctrl+Shift+T`; `Esc` ยกเลิก, `Backspace` ล้างค่า

## Overlay placement

ใน settings มีตัวเลือก **Overlay**:
- **In-place (over source text)** — ค่าเริ่มต้น; ใช้ bounding box ของแต่ละ dialogue block เพื่อวางคำแปลให้ตรงตำแหน่ง
- **Bottom center** — วางกรอบคำแปลกลางล่างจอ
- **Overlay font / Size** — เลือกฟอนต์และขนาด (0 = auto-fit)
- **Original text** — Show = แสดงข้อความต้นฉบับเหนือคำแปล (ค่าเริ่มต้น), Hide = ซ่อน ให้คำแปลได้พื้นที่ทั้งกล่อง (วางทับข้อความเดิม + Font size = 0 คำแปลจะขยายเต็มกล่อง; Bottom center ได้แค่พื้นที่เพิ่ม ขนาดตัวอักษรเท่าเดิม)
- **Box opacity (%)** — ความทึบของกล่องพื้นหลัง 20–100% (ค่าเริ่มต้น 80)

ถ้าภาพยังไม่ตรงตำแหน่ง ให้เลือก ROI ให้แน่นขึ้นหรือสลับเป็น Bottom center

## ใช้งาน

1. เชื่อมต่อ PS5 ตามปกติ
2. กดไอคอน **訳** มุมขวาบน (หรือเปิดจาก Chiaki menu)
3. ในการ์ด **Translator** กด **Turn on**
4. ค่าเริ่มต้นคือ **Google Translate (free)** ใช้ได้ทันทีโดยไม่ต้องใส่ key
   - อยากได้คำแปลดีกว่า: เลือก **Gemini Flash** หรือ **DeepL** ในหัวข้อ Setup → Translation แล้วใส่ API key
     (บันทึกอัตโนมัติเมื่อออกจากช่อง; ถ้ายังไม่มี key การ์ด **Translation engine** จะขึ้น ⚠ พร้อมปุ่ม **Add key** และ **Use free Google**)
   - **Offline NLLB** → ต้องใช้ build ที่เปิด CTranslate2 และมีโมเดลแยก (ไม่รวมใน AppImage นี้)
5. ในการ์ด **Subtitle area** กด **Select area** แล้วลากกรอบซับ
6. ระบบจะแปลอัตโนมัติเมื่อข้อความนิ่ง 250 ms (badge **LIVE**); ถ้าอยากแปลเฉพาะตอนกด L4 ให้แตะ badge เป็น **MANUAL**
7. ค่าที่แก้ในหน้า settings จะมีผลเมื่อปิดหน้า settings

หน้า settings ใช้ controller ได้ทั้งหมด:

| ปุ่ม | หน้าที่ |
| --- | --- |
| D-pad ขึ้น/ลง | เลื่อนไปแถวบน/ล่าง |
| D-pad ซ้าย/ขวา | เปลี่ยนค่า (ตัวเลือก/ตัวเลข) หรือเลื่อนระหว่างปุ่มในแถวเดียวกัน |
| A | กดปุ่ม / เปิดรายการตัวเลือก |
| B | ปิดหน้า settings |
| L1 / R1 | สลับหัวข้อ Setup · Tuning · Appearance · Advanced |

ปุ่มควบคุมระหว่างเล่น:

| ปุ่ม | หน้าที่ |
| --- | --- |
| L4 (Paddle 1) | แปลทันที (manual trigger) |
| R4 (Paddle 2) | ซ่อน/แสดงคำแปล |
| L5 (Paddle 3) | เปิด backlog |
| R5 (Paddle 4) | ปรับ ROI |
| F13 / F14 / F15 | สำรอง ถ้า Steam Input ส่งเป็นคีย์บอร์ด |

แตะบรรทัดต้นฉบับเพื่อเปิด JMdict

## สิ่งที่อยู่ใน AppImage

- `chiaki-ngx` GUI + translator
- PaddleOCR PP-OCRv3 detection + Japan recognition models
- ONNX Runtime 1.32 Linux x86_64
- JMdict เต็ม (219,217 entries, ~83 MB)
- Qt6 / FFmpeg / libplacebo / SDL และไลบรารีที่จำเป็น

ไม่รวม: โมเดล NLLB (ลิขสิทธิ์ CC BY-NC 4.0) และ API key ของผู้ใช้

## การตรวจสอบที่ทำแล้ว

- Build บน **Ubuntu 20.04 builder** (glibc 2.31) จึงรองรับ SteamOS ปัจจุบัน
- CI ตรวจว่าใน AppImage มี `AppRun`, โมเดล OCR, JMdict และ `libonnxruntime.so.1`
- CI รัน `AppRun --help` ด้วย Qt offscreen ผ่าน (smoke test)
- Binary ต้องการ glibc สูงสุด **2.31** (ตรวจด้วย `readelf --version-info`)

## ข้อจำกัด

- ยังไม่ได้ทดสอบบน Steam Deck เครื่องจริง (VA-API 1080p60, Gamescope, touch, paddle)
- ต้องมี API key สำหรับโหมด cloud; โหมด offline NLLB ต้อง build/model แยก
- AppImage เป็น x86_64 เท่านั้น (Steam Deck ใช้ x86_64)
