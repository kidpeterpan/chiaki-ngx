# Steam Deck / Linux x86_64 (AppImage)

## ดาวน์โหลด

**[chiaki-ngx-translator-x86_64.AppImage](https://github.com/kidpeterpan/chiaki-ngx/releases/download/v0.2.11-translator/chiaki-ngx-translator-x86_64.AppImage)** — 286 MB

SHA-256:

```text
29919d99e9db0f5eb7dff491a682017c0461053ff980365b44d1deea3b0557cf
```

ใช้ **v0.2.11** ซึ่งมี real-time toggle, touch fix, Chrome Screen AI และ Decky-Translator providers ครบ

ไฟล์ `.sha256` อยู่ใน release เดียวกัน

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

ตั้งแต่ **v0.2.1** ขึ้นไป (v0.2.11 แนะนำ):

- แตะไอคอน **訳** มุมขวาบนได้เลย แม้เกมกำลังใช้ touchscreen เป็น PS touchpad
- เมื่อเมนูเปิด ระบบจะ `grabInput()` ให้ touch ทั้งหมดมาที่ overlay จึงกดปุ่ม/เลือก ROI/เปิด backlog ได้
- แตะที่คำแปลเพื่อเปิด JMdict
- ปิดเมนูแล้ว touch จะกลับไปเป็น PS touchpad ตามเดิม

ถ้าใช้เวอร์ชันเก่าแล้วแตะไม่ติด ให้อัปเดตเป็น v0.2.11

## Chrome Screen AI

**v0.2.11 AppImage มี Chrome Screen AI พร้อม assets ฝังอยู่ในตัวแล้ว** (Linux x86_64)
เลือกได้ที่ช่อง OCR → `Chrome Screen AI (Linux)`; ไม่ต้องดาวน์โหลดเพิ่ม

## เปิด settings จาก Chiaki menu

นอกจากกดไอคอน **訳** มุมขวาบนแล้ว เปิดแผง translator ได้จาก **stream menu ของ Chiaki**
(ปุ่ม `Ctrl+O` หรือปุ่ม stream menu บนจอย) → กดปุ่ม **訳** ในแถบเมนู

## Hotkeys

แก้ได้ในแผง translator → หัวข้อ **Hotkeys**:

- รองรับปุ่มเดี่ยว (`F13`) และ combination (`Ctrl+Shift+T`)
- มีปุ่ม **Live** ใน Chiaki stream menu และ badge LIVE/OFF ใต้มุมขวาบนสำหรับเปิด/ปิด real-time translation ทันที
- เว้นว่าง = ปิด shortcut นั้น
- ตรวจ conflict ระหว่าง 4 actions: Translate, Overlay, Backlog, Settings
- บันทึกใน QSettings

ค่าเริ่มต้น: `F11` เปิด/ปิด real-time, `F13` แปล, `F14` ซ่อน/แสดง, `F15` backlog, `F12` เปิด settings

## Timing

ใน settings ปรับได้:

- **Hide after (s)** — ให้ overlay แปลหายอัตโนมัติหลัง N วินาที (`0` = ไม่หาย)
- **Scan interval (ms)** — ความถี่ในการตรวจภาพ/ข้อความใหม่ (50–2000 ms)
- **Stable time (ms)** — ข้อความต้องนิ่งนานเท่าไรก่อน OCR/แปล (100–5000 ms)

## Hotkey capture

กด **Set** ข้างช่อง hotkey แล้วกดคีย์ผสมที่ต้องการได้เลย
เช่น `Ctrl+Shift+T`; `Esc` ยกเลิก, `Backspace` ล้างค่า

## Overlay placement

ใน settings มีตัวเลือก **Overlay**:
- **In-place (over source text)** — ค่าเริ่มต้น; ใช้ bounding box ของแต่ละ dialogue block เพื่อวางคำแปลให้ตรงตำแหน่ง
- **Bottom center** — วางกรอบคำแปลกลางล่างจอ
- **Overlay font / Size** — เลือกฟอนต์และขนาด (0 = auto-fit)

ถ้าภาพยังไม่ตรงตำแหน่ง ให้เลือก ROI ให้แน่นขึ้นหรือสลับเป็น Bottom center

## ใช้งาน

1. เชื่อมต่อ PS5 ตามปกติ
2. กดไอคอน **訳** มุมขวาบน
3. เปิด **Enabled**
4. เลือก backend:
   - **Gemini Flash** หรือ **DeepL** → ใส่ API key
   - **Offline NLLB** → ต้องใช้ build ที่เปิด CTranslate2 และมีโมเดลแยก (ไม่รวมใน AppImage นี้)
5. กด **Select ROI** แล้วลากกรอบซับ
6. ระบบจะแปลอัตโนมัติเมื่อข้อความนิ่ง 250 ms

ปุ่มควบคุม:

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
