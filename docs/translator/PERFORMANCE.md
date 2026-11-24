# Performance budget และการวัด

## งบประมาณบน Steam Deck (Aerith, RAM 16 GB แชร์ GPU)

| ส่วน | RAM | CPU (4C/8T) | Latency เป้า |
| --- | --- | --- | --- |
| chiaki-ng 1080p60 VA-API | ~350 MB | 8–12% | 16 ms/เฟรม |
| ONNX OCR INT8 | ~200 MB | 15–25% เฉพาะตอน OCR | 80–150 ms |
| Translator cloud | ~30 MB | <1% | 300–600 ms |
| Translator NLLB offline (optional) | ~1.2 GB (วัดจริง host) | 30–45% | 400–800 ms (เป้า) |
| **รวมโหมด cloud** | **~580 MB** | **เฉลี่ย ≤15%** | **≤0.8 s** |

## สิ่งที่ออกแบบไว้เพื่อให้อยู่ในงบ

- **10 Hz frame tap** ไม่ใช่ 60 Hz; คัดลอกเฉพาะ ROI (ปกติ < 900×220 px)
- **ถ้าไม่นิ่งไม่ OCR** — ลดทั้ง CPU และโควตา API
- **Cache + dedup** — ข้อความซ้ำ/ใกล้ซ้ำไม่ถูกส่งแปลหรือ OCR ซ้ำ
- **ONNX threads = 2** และ pipeline worker ผูก affinity ได้ (ค่าเริ่มต้นคอร์ 2,3)
- **คิว 4 เฟรม** — ถ้า worker ตามไม่ทันจะทิ้งเฟรมใหม่ ไม่ block decoder
- **ไม่ใช้ OpenCV** ในเส้นทางหลัก — preprocessing เป็น integral-image/percentile
  ที่เขียนเอง เบากว่าและลด dependency ตอนแพ็ก

## ตัวเลขที่วัดได้ (host พัฒนา)

| รายการ | ค่า | วิธีวัด |
| --- | --- | --- |
| Unit tests | 61 tests ผ่านใน ~1.0–1.3 s | `ctest --test-dir build-core` |
| ONNX model load | 30–58 ms | `translator_ocr_probe` stderr `load_ms` |
| OCR (det+rec) ภาพ 720×80 | **26–29 ms** | probe `ocr_ms` (Apple M1, ORT 1.32, 2 threads) |
| JMdict build (219,217 entries) | ~2.5 s, 83 MB | `build_jmdict.py` |
| JMdict lookup | <1 ms (index) | `translator_dict_probe` |
| NLLB offline | 267–429 ms, peak RSS 1.23 GB | `translator_nllb_probe` (M1, beam 2) |
| Full core build | ~20 วิ ( incremental ) | CMake/Ninja |

> ตัวเลข OCR เป็นของ Apple M1 ไม่ใช่ Steam Deck. Zen 2 คาดว่าช้ากว่า 2–3 เท่า
> จึงควรอยู่ราว 60–150 ms ตามงบ; ต้องวัดจริงบน Deck ก่อน release

## วิธีวัดบน Deck

```bash
# 1) เปิด log/สถานะใน overlay แล้วดู PipelineStats ผ่าน QML (statusText)
# 2) วัด process memory
ps -o rss= -p "$(pgrep -x chiaki)"        # KB
# 3) วัด dropped frames ระหว่างเล่น 2 นาที
#    (Chiaki.window.droppedFrames แสดงใน Stream Stats)
# 4) ถ้าต้องการ profile เชิงลึก
perf stat -p "$(pgrep -x chiaki)" sleep 30
```

เกณฑ์ผ่าน: dropped frames ไม่เพิ่มเกิน 1% เทียบ baseline, OCR ≤150 ms,
คำแปล cloud ≤800 ms p95, RAM รวม ≤650 MB

## ปรับแต่งเมื่อช้า

- ลด `detection_limit_side` (960 → 640) ถ้าข้อความยังอ่านได้
- เพิ่ม `frame.sample_interval_ms` (100 → 150) ลดภาระแต่ช้าลง
- ใช้ `OcrPreprocess::Threshold` เฉพาะเกมที่พื้นหลังโปร่งใส
- จำกัด `worker_cpu_affinity` เป็นคอร์เดียวถ้าเกมกระตุก
- เปิด cache TTL นานขึ้น (30 s) สำหรับบทสนทนาที่วนซ้ำ
