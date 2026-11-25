# Live translation: text-based stability, slow OCR fallback, and an on/off setting

Date: 2026-11-25. Size: a project (pipeline core logic, tuning, tests).

## Intent and why

On the Steam Deck, live (automatic) translation does not fire in the user's game, while manual
translation (L4 / Translate now) works. The user confirmed the game's dialogue box is
see-through over a moving scene, and the selected area is larger than the box.

Root cause: live translation waits until the whole selected area is still for `stable_ms`
(250 ms). "Changed" means more than 0.2% of pixels moved by more than 14 grey levels, or a mean
absolute difference above 1.0, measured over every pixel in the area (`FrameProcessor::evaluate`).
A moving scene behind a see-through box, or inside the area, resets the timer on every sample,
so OCR never runs. Manual translation skips that gate.

Evidence (H.264 8 Mb/s 60 fps stream simulations decoded at 10 Hz into the real
`FrameProcessor` with the app's thresholds): static screen OK; 80% box over gameplay fires
sometimes; 50% box over gameplay, area = box: 39/39 samples "changed", never fires; area
including the scene: never fires. A spike measuring stability only on bright, text-like pixels
fired in both failing cases.

The user also wants live translation to be switchable on and off.

## Goals

- Live translation fires when the dialogue text has settled, whatever moves behind or around it.
- It can never go silent for good: if the area never settles, a slow OCR fallback still
  translates a line once it reads the same text twice in a row.
- A **Live translation: On / Off** setting (default On). Off: only L4 / Translate now translate,
  and no automatic OCR runs.
- Manual translation keeps working exactly as today.

## Non-goals and anti-goals

- No change to OCR engines, translation backends, the overlay, or the duplicate suppression rules.
- Anti-goal: OCR running continuously while nothing is being read. The fallback must be slow and
  bounded; on the Deck every OCR run costs CPU and battery.
- Anti-goal: re-translating or re-showing a line that is already on screen.

## Constraints

- `translator_core` stays Qt-free; the worker thread and ring buffer stay as they are.
- The Deck's CPU budget: OCR is 0.3-0.8 s per run on two cores.
- Settings follow the panel's model: stored in QSettings, applied when the panel closes
  (the Live switch restarts nothing more than the existing `applySettings` already does).

## Approaches (picked by the user: 3)

1. Text-layer stability only: cheap, fixes the reported case per the spike; weak on dark or
   coloured text and on bright moving things behind the box.
2. Slow OCR fallback only: works for any background and text colour; costs CPU whenever the scene
   moves.
3. **Both (chosen):** text-layer stability as the primary trigger, the slow OCR fallback as the
   safety net.

## Design

**Text-layer stability (`FrameProcessor`).** Each sample builds a mask of text-like pixels: the
extreme-brightness minority of the area. Polarity (light text on dark, or dark on light) is
decided per sample from the area's brightness distribution. "Changed" compares masks between
samples: the share of mask pixels that flipped, against a threshold. The rest of the existing
logic (generation, `stable_ms`, emit once per generation, `min_stddev`) stays. The old whole-area
measures stay in `ProcessResult` for diagnostics.

**Slow OCR fallback (`TranslatorPipeline`).** It runs while live mode is on and the *whole area*
has not settled for `fallback_after_ms` (1500 ms), whatever the mask says, so a text change the
mask misses (dark or coloured text) is still read. One OCR per `fallback_interval_ms` (1500 ms).
It keeps the text of the previous fallback read, per OCR block; when two consecutive reads match
(the existing `similarity` with the dedup threshold, 0.9), that line goes through the normal path
(duplicate check, cache, translate). A settled area resets it. Manual translation bypasses all of
it, as today.
- Back-off (my call): after 3 fallback reads in a row with no text, the interval becomes 5000 ms;
  it returns to 1500 ms when the mask changes or a read finds text.
- No re-show (my call): the fallback remembers the last line it delivered and does not deliver it
  again while it keeps reading the same text, even past the 30 s duplicate TTL.
- Cloud OCR (my call): no fallback with `google-vision`, `gemini-vision` or `ocrspace`; every read
  would be a network call (40 a minute) and hit rate limits. Those providers use the text-layer
  trigger only.

**Live switch (user's choice A: "Live" means automatic translation).** `TranslatorManager`
gains `liveTranslation` (bool, `translator/live`, default true) mapped to the existing
`PipelineConfig::manual_only` (inverted). The existing "Live" controls are re-pointed from
`Translator.enabled` to it: the stream-menu Live switch (`StreamMenuWindow.qml`), the overlay
badge and the F11 "Live on/off" hotkey. Turning Live on while the translator is off also turns the
translator on. Live off keeps the translator on, so L4 / Translate now still work. The badge shows
LIVE (on, automatic), MANUAL (on, translate on demand) or OFF. Turning the whole translator off
stays in the settings checklist (Turn off). The settings panel's Setup section also gets a
"Live translation" combo (On, Off).

**Diagnostics.** The status line says when live mode is waiting on a moving area and when the
fallback is reading, so the next report from the Deck says where frames stop.

## Tests

- `translator_core` unit tests for the mask stability: text on a see-through box over changing
  background settles; changed text still counts as changed; dark-on-light text settles.
- A pipeline test with a fake OCR engine: a never-settling area triggers the fallback, which
  translates once after two matching reads, and not again for the same line.
- The live switch: Off means no automatic OCR, manual still works (pipeline and QML tests).
- Re-run the stream simulations (`/tmp` harness) against the new code as evidence.

## Calls made after the builder check

- Fallback back-off, no re-show past the duplicate TTL, no fallback for cloud OCR, and the
  fallback running whenever the whole area is unsettled: see Design. Marked as my calls.

## Left to the builder

- Exact mask thresholds and the flip-ratio threshold, set from the simulations and fixtures.
- Whether the fallback timing becomes configurable later (not now).
