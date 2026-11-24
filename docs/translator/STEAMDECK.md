# Steam Deck / Linux x86_64 (AppImage)

## ดาวน์โหลด

**[chiaki-ngx-translator-x86_64.AppImage](https://github.com/kidpeterpan/chiaki-ngx/releases/download/v0.1.0-steamdeck/chiaki-ngx-translator-x86_64.AppImage)** — 201 MB

SHA-256:

```text
ae1f41a2bd3b4a02e7e33219aaf8232cfbcca234fa88b934f54d66096af56bff
```

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
