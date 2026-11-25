# OCR และ Translation providers

chiaki-ngx รองรับทั้ง OCR ในเครื่องและ cloud ตามแนวทางของ
[Decky-Translator](https://github.com/cat-in-a-box/Decky-Translator)

## OCR

| Provider | ประเภท | สถานะ | ต้องมี |
| --- | --- | --- | --- |
| PaddleOCR PP-OCRv3 (Japan rec) | on-device ONNX | ค่าเริ่มต้น, ทดสอบแล้ว | models ใน `assets/models/` |
| RapidOCR PP-OCRv5 | on-device ONNX | เลือกได้ใน Settings → OCR | `scripts/translator/fetch-rapidocr.sh` |
| Google Cloud Vision | cloud | ยังไม่ implement | API key + Vision API |
| OCR.space | cloud | ยังไม่ implement | ฟรี/คีย์ |
| Gemini Vision | cloud (OCR + แปลรวม) | ยังไม่ implement | API key |
| Chrome Screen AI | on-device | ยังไม่ implement | Chromium Screen AI |

RapidOCR ใช้ PP-OCRv5 mobile det + `ch_rec.onnx` + `ch_dict.txt`
(~21 MB) ซึ่งอ่านภาษาญี่ปุ่นได้จากชุดอักขระ CJK ผสมอังกฤษ
เลือกได้ที่ช่อง **OCR** ใน overlay โดยไม่ต้อง build ใหม่

## Translation

| Provider | ประเภท | สถานะ | ต้องมี |
| --- | --- | --- | --- |
| Gemini Flash | cloud | มีแล้ว | API key |
| Google Translate (free) | cloud | เพิ่มแล้ว | เน็ต (ไม่ต้องใช้ key) |
| Google Cloud Translation | cloud | เพิ่มแล้ว | API key + Cloud Translation API |
| DeepL | cloud | มีแล้ว | API key |
| OpenAI-compatible / OpenCode Go | cloud | มีแล้ว | API key / base URL |
| NLLB-200 + CTranslate2 | on-device | มีแล้ว (build แยก) | โมเดล ~600 MB |
| Gemini Vision | cloud (รวม OCR) | ยังไม่ implement | API key |

### Google Translate (free)

ใช้ endpoint เดียวกับเว็บ Google Translate:
`translate.googleapis.com/translate_a/single` — ไม่ต้องมี API key
แต่เป็น unofficial endpoint และอาจถูก rate-limit

### Google Cloud Translation

ใช้ `translation.googleapis.com/language/translate/v2` พร้อม API key
ต้องเปิด Cloud Translation API และตั้ง billing ใน Google Cloud project

## การตั้งค่า

1. เปิด overlay → **訳**
2. เลือก **OCR**: PaddleOCR หรือ RapidOCR
3. เลือก **Backend**: Gemini, Google Translate, Google Cloud, DeepL, OpenCode Go, OpenAI-compatible หรือ Offline NLLB
4. ใส่ API key เฉพาะ provider ที่ต้องใช้
5. กด **Save** แล้ว **Apply**

## ที่ยังไม่ได้ทำ

- Cloud OCR (Google Vision / OCR.space) และ Gemini Vision แบบส่งภาพ
- Chrome Screen AI
- ทดสอบ provider จริงด้วย API key บน Steam Deck
