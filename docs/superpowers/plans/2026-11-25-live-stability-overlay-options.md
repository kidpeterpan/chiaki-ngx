# Live translation stability, Live switch and overlay options — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Live translation fires on settled text even over moving scenes, never goes silent for good, can be switched on and off as "Live", and the overlay can hide the original line and change its box opacity.

**Architecture:** `FrameProcessor` measures stillness on a text mask and reports how long the whole area has been unsettled; `TranslatorPipeline` adds a bounded slow-OCR fallback and a runtime manual-only switch; `TranslatorManager` exposes `liveTranslation`, `overlayShowSource`, `overlayBoxOpacity`; QML re-points the existing Live controls and adds settings rows.

**Tech Stack:** C++17 `translator_core` (Qt-free), Qt 6 QML, munit-style `TEST()` harness in `translator_core/tests`, qmltestrunner suite in `gui/tests/translator/qml`.

**Spec:** `docs/superpowers/specs/2026-11-25-live-translation-text-stability-design.md`, `docs/superpowers/specs/2026-11-25-overlay-original-text-and-box-opacity-design.md`

## Global Constraints

- `translator_core` stays Qt-free; worker thread and `SpscRingBuffer<QueuedFrame, 4>` unchanged.
- Manual translation (L4 / Translate now) behaves exactly as today in every mode.
- Fallback: `fallback_after_ms` 1500, `fallback_interval_ms` 1500, back-off to 5000 ms after 3 empty reads; match via `textSimilarity` >= 0.9.
- No fallback for OCR providers `google-vision`, `gemini-vision`, `ocrspace`.
- "Live" means automatic translation. Live off keeps the translator on. Badge: LIVE / MANUAL / OFF. Turning Live on turns the translator on.
- Defaults keep today's behaviour: live on, original text shown, box opacity 80.
- Settings persist in QSettings keys `translator/live`, `translator/overlay_show_source`, `translator/overlay_box_opacity`.
- `scripts/verify.sh --full` passes before finishing.

## Review Focus

- A line typed out character by character must not trigger OCR until typing stops → covered by the existing `frame_stabilizer_never_fires_while_text_is_typing` (must still pass, Task 1).
- A static screen whose text changes to a new line must still be read (no regression vs today) → `frame_text_change_starts_new_generation` (Task 1).
- Live off must mean zero automatic OCR, even while the fallback conditions hold → `pipeline_live_off_runs_no_automatic_ocr` (Task 2).
- A line left on screen over a moving scene for longer than the duplicate TTL is not shown again → `pipeline_fallback_does_not_reshow_a_line_past_the_ttl` (Task 2).
- Toggling Live from the badge while the translator is off turns it on and shows LIVE → `test_live_badge_turns_translator_on` (Task 4).

## File Structure

- `translator_core/include/trans/FrameProcessor.hpp`, `translator_core/src/FrameProcessor.cpp` — text-mask stillness, `unsettled_ms`.
- `translator_core/include/trans/TranslatorPipeline.hpp`, `translator_core/src/TranslatorPipeline.cpp` — fallback state, runtime manual-only.
- `gui/include/translator/preferences.h`, `gui/src/translator/preferences.cpp` — `clampOverlayBoxOpacity`.
- `gui/include/translator/translatormanager.h`, `gui/src/translator/translatormanager.cpp` — three properties, config wiring, status states.
- `gui/src/qml/translator/TranslatorOverlay.qml`, `gui/src/qml/StreamMenuWindow.qml` — Live controls, overlay options, settings rows.
- Tests: `translator_core/tests/TestFrame.cpp`, `translator_core/tests/TestPipeline.cpp`, `gui/tests/translator/TestPreferences.cpp`, `gui/tests/translator/qml/tst_deck_panel.qml` (+ stub `Translator.qml`).
- Docs: `README.md`, `docs/translator/STEAMDECK.md`.

---

### Task 1: Text-mask stillness in FrameProcessor

**Files:**
- Modify: `translator_core/include/trans/FrameProcessor.hpp` (`FrameProcessorConfig`, `ProcessResult`, `FrameProcessor` private state)
- Modify: `translator_core/src/FrameProcessor.cpp` (`FrameProcessor::evaluate`)
- Test: `translator_core/tests/TestFrame.cpp`

**Interfaces:**
- Produces: `FrameProcessorConfig::text_flip_ratio` (double, default 0.02), `text_bright_threshold` (int, 200), `text_dark_threshold` (int, 55); `ProcessResult::text_flip_ratio` (double), `ProcessResult::unsettled_ms` (int64_t: time the whole area has kept changing, 0 once a whole-area sample is still).

- [ ] **Step 1: Write the failing tests** in `TestFrame.cpp`, using `MakeGray` plus small helpers that draw a bright glyph block onto a frame and fill the background with a per-sample changing pattern at reduced contrast (a see-through box):
  - `frame_text_settles_over_moving_background`: 10 samples 100 ms apart, same glyphs, background changes each sample (whole-area `changed_ratio` > 0.002 every time) → some sample has `stable == true`; `unsettled_ms` > 0 on the last sample.
  - `frame_text_change_starts_new_generation`: static background, glyphs A for 5 samples then glyphs B for 5 samples → two samples with `stable == true`, and their `generation` values differ.
  - `frame_dark_text_on_light_box_settles`: light background (fill 230) with dark glyphs (fill 20), background changes slightly per sample → a `stable == true` sample.
- [ ] **Step 2: Run** `cmake --build build-core && ./build-core/translator_tests frame_` — expect the three new tests to FAIL, all existing `frame_` tests PASS.
- [ ] **Step 3: Implement in `FrameProcessor::evaluate`.** Polarity per sample: median < 128 → text is bright (`>= text_bright_threshold`), else dark (`<= text_dark_threshold`). Mask on the diff frame. `text_flip_ratio` = pixels whose mask bit flipped / max(1, pixels set in either mask). `content_changed` = `text_flip_ratio > config_.text_flip_ratio`; an empty mask in both samples counts as no content. Keep the whole-area `changed_ratio`/`mean_abs_delta` computation: when it exceeds the old thresholds add `delta_ms` to `unsettled_ms_`, else reset it to 0. Store the previous mask alongside `last_gray_`; `reset()` clears both.
- [ ] **Step 4: Run** `./build-core/translator_tests frame_` — all PASS, including `frame_stabilizer_never_fires_while_text_is_typing` and `frame_stabilizer_ignores_blank_frames`.
- [ ] **Step 5: Re-run the stream simulations** against the new code: rebuild `/tmp/trans-repro/live/probe` from `build-core/libtranslator_core.a`, run `bash /tmp/trans-repro/live/run.sh <fixture>` and `run2.sh` — expect `stable(OCR)` >= 1 for "50% box, area = box" and "50% box, area incl. scene"; record the output in the commit message body.
- [ ] **Step 6: Commit** `translator_core` changes and tests: `git commit -m "Measure live-translation stillness on text pixels, not the whole area"`.

### Task 2: Slow OCR fallback and runtime manual-only in TranslatorPipeline

**Files:**
- Modify: `translator_core/include/trans/TranslatorPipeline.hpp` (`PipelineConfig`, `TranslatorPipeline`)
- Modify: `translator_core/src/TranslatorPipeline.cpp` (`handleFrame`, `PipelineShared`)
- Test: `translator_core/tests/TestPipeline.cpp`

**Interfaces:**
- Consumes: `ProcessResult::unsettled_ms` (Task 1).
- Produces: `PipelineConfig::live_fallback` (bool, default true), `fallback_after_ms` (int64_t 1500), `fallback_interval_ms` (int64_t 1500), `fallback_idle_interval_ms` (int64_t 5000), `fallback_idle_after_reads` (int 3); `void TranslatorPipeline::setManualOnly(bool manual_only)` (thread-safe; replaces reading `config.manual_only` in the worker with an atomic seeded from the config); `onState` values `"unsettled"` (whole area moving, waiting) and `"fallback"` (a fallback read ran).

- [ ] **Step 1: Write the failing tests** with the existing `FakeOcrEngine`/`FakeBackend`/`Collector` and images whose background changes every submission (so the area never settles) while the text stays:
  - `pipeline_fallback_translates_a_line_after_two_matching_reads`: submit every 100 ms for 4000 ms → exactly 1 translated item; `stats().ocr_runs` between 2 and 4.
  - `pipeline_live_off_runs_no_automatic_ocr`: `config.manual_only = true`, same input → 0 items, `ocr_runs == 0`; then `manualTrigger()` + one submit → 1 item.
  - `pipeline_set_manual_only_takes_effect_while_running`: start live, `setManualOnly(true)` before submitting → 0 automatic items.
  - `pipeline_fallback_backs_off_after_empty_reads`: fake OCR returns no lines, 9000 ms of moving input → `ocr_runs` <= 5 (3 reads at 1500 ms, then at 5000 ms).
  - `pipeline_fallback_off_for_cloud_ocr`: `config.live_fallback = false`, moving input → 0 items, `ocr_runs == 0`.
  - `pipeline_fallback_does_not_reshow_a_line_past_the_ttl`: `config.dedup_ttl_ms = 1000`, same text for 6000 ms of moving input → exactly 1 item.
- [ ] **Step 2: Run** `./build-core/translator_tests pipeline_` — new tests FAIL, existing PASS (including `pipeline_does_not_translate_without_change_or_manual_trigger`).
- [ ] **Step 3: Implement in `handleFrame`.** After `processGray`: if manual-only (atomic) and not manual → return. If `result.stable` or manual → today's path. Else, if `live_fallback` and `result.unsettled_ms >= fallback_after_ms` and `now - last_fallback_ms >= current interval` → emit `"fallback"`, OCR, compare each block's text with the previous fallback read (`textSimilarity >= 0.9`); a block that matches its previous read and is not the last line the fallback delivered goes through the normal delivery path (duplicate check, cache, translate) and becomes the last delivered line. Count empty reads to switch to `fallback_idle_interval_ms`; any read with text, or a `stable` result, resets to `fallback_interval_ms`. A non-stable sample that does not run the fallback emits `"unsettled"` at most once per second.
- [ ] **Step 4: Run** `./build-core/translator_tests pipeline_` — all PASS; then `ctest --test-dir build-core` — all PASS.
- [ ] **Step 5: Commit** `git commit -m "Add a slow OCR fallback and a runtime manual-only switch to the pipeline"`.

### Task 3: Manager properties and wiring

**Files:**
- Modify: `gui/include/translator/preferences.h`, `gui/src/translator/preferences.cpp`; Test: `gui/tests/translator/TestPreferences.cpp`
- Modify: `gui/include/translator/translatormanager.h`, `gui/src/translator/translatormanager.cpp`

**Interfaces:**
- Consumes: `PipelineConfig::live_fallback`, `TranslatorPipeline::setManualOnly` (Task 2).
- Produces (QML): `Q_PROPERTY(bool liveTranslation READ liveTranslation WRITE setLiveTranslation NOTIFY liveTranslationChanged)`, `Q_INVOKABLE void toggleLive()`, `Q_PROPERTY(bool overlayShowSource ... NOTIFY overlayStyleChanged)`, `Q_PROPERTY(int overlayBoxOpacity ... NOTIFY overlayStyleChanged)`; `int translator::clampOverlayBoxOpacity(int percent)` (20..100).

- [ ] **Step 1: Write the failing test** in `TestPreferences.cpp`: `clampOverlayBoxOpacity(0) == 20`, `(80) == 80`, `(150) == 100`.
- [ ] **Step 2: Run** `cmake --build build-cloud-tests --target translator_preference_tests` — FAIL (undefined).
- [ ] **Step 3: Implement** `clampOverlayBoxOpacity`; then in the manager: load the three settings in the constructor (defaults true, true, 80); `setLiveTranslation(bool)` persists, emits, calls `pipeline_->setManualOnly(!live)` and, when turning on with the translator off, `setEnabled(true)`; `toggleLive()` = `setLiveTranslation(!(enabled && live))`; `ensurePipeline` sets `config.manual_only = !live_translation_` and `config.live_fallback = !cloud_ocr`; map `"unsettled"` → "Live: waiting for the text to settle" and `"fallback"` → "Live: reading a moving area" in `callbacks.onState`.
- [ ] **Step 4: Run** `./build-cloud-tests/gui/translator_preference_tests` and `scripts/verify.sh --stage build-release` — PASS.
- [ ] **Step 5: Commit** `git commit -m "Expose live translation, original text and box opacity settings"`.

### Task 4: QML — Live controls, settings rows, overlay options

**Files:**
- Modify: `gui/src/qml/translator/TranslatorOverlay.qml` (badge, F11 shortcut, Setup and Appearance rows, region blocks, bottom subtitle), `gui/src/qml/StreamMenuWindow.qml:118-125`
- Test: `gui/tests/translator/qml/tst_deck_panel.qml`, `gui/tests/translator/qml/stubs/org/streetpea/chiaking/Translator.qml`

**Interfaces:**
- Consumes: the properties from Task 3.

- [ ] **Step 1: Extend the stub** `Translator.qml` with `liveTranslation: true`, `overlayShowSource: true`, `overlayBoxOpacity: 80`, `function toggleLive()` mirroring Task 3's rule; then write the failing QML tests:
  - `test_live_badge_turns_translator_on`: enabled false; tap the badge (`mouseClick` on the item whose text is "OFF") → `Translator.enabled` and `liveTranslation` true, badge text "LIVE".
  - `test_live_off_shows_manual_badge`: enabled true, `liveTranslation = false` → badge text "MANUAL".
  - `test_setup_live_row_switches_live`: open panel, Setup, focus the "Live translation" combo, Right → `liveTranslation == false`.
  - `test_hidden_source_gives_translation_the_box`: one overlay region, `overlayShowSource = false` → the region's source `Text` is not visible and the translation `Text` height equals the block's inner height.
  - `test_box_opacity_sets_background_alpha`: `overlayBoxOpacity = 40` → the bottom subtitle's `color.a` ≈ 0.40 (± 0.01).
- [ ] **Step 2: Run** `qmltestrunner -platform offscreen -import gui/tests/translator/qml/stubs -input gui/tests/translator/qml/tst_deck_panel.qml` with `QT_QUICK_CONTROLS_CONF=gui/src/qml/qtquickcontrols2.conf` — new tests FAIL.
- [ ] **Step 3: Implement.** Badge: text LIVE/MANUAL/OFF from `enabled`/`liveTranslation`, tap and F11 call `toggleLive()`. Stream menu switch: `checked: Translator.enabled && Translator.liveTranslation`, `onToggled: Translator.liveTranslation = checked`. Setup row "Live translation" (`DeckComboBox`, model `["On", "Off"]`) first in the section. Appearance rows "Original text" (`["Show", "Hide"]`) and "Box opacity (%)" (`DeckSpinBox` 20–100 step 10). Overlay: source `Text`/`sourceEdit` `visible: Translator.overlayShowSource`; region translation height = block inner height minus the source line only when shown; bottom-mode tap lookup only when shown; both boxes' background `Qt.rgba(11/255, 12/255, 16/255, Translator.overlayBoxOpacity / 100)`.
- [ ] **Step 4: Run** the QML suite (Material conf) — all PASS; `scripts/verify.sh --stage lint --stage test-gui` — PASS.
- [ ] **Step 5: Commit** `git commit -m "Re-point Live to automatic translation and add overlay text and opacity options"`.

### Task 5: Docs and full verification

**Files:**
- Modify: `README.md` (Extended features: Live, overlay options), `docs/translator/STEAMDECK.md` (badge states, F11 meaning, Live row, Appearance rows)

- [ ] **Step 1: Update the docs** to the behaviour above (Thai in STEAMDECK.md, English in README).
- [ ] **Step 2: Run** `scripts/verify.sh --full` — 9/9 PASS; `scripts/verify/selftest.sh` — all cases pass.
- [ ] **Step 3: Commit** `git commit -m "Document Live translation and overlay options"`.
