# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

Fresh clone: `git submodule update --init --recursive` (needed for `third-party/`, `test/munit`, `switch/borealis`, `android/.../oboe`).

**Desktop app (lib + Qt GUI + CLI):**
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target chiaki
```
The translator is compiled in by default (`CHIAKI_ENABLE_TRANSLATOR=ON`). OCR needs ONNX Runtime: add `-DTRANS_ENABLE_ONNXRUNTIME=ON` (and `-DONNXRUNTIME_ROOT=...` if it is not in a standard prefix). Offline NLLB translation additionally needs `-DTRANS_ENABLE_CTRANSLATE2=ON`.

**Unit tests for `lib/` (munit, target `chiaki-unit`):**
```bash
cmake -S . -B build -DCHIAKI_ENABLE_TESTS=ON
cmake --build build --target chiaki-unit
build/test/chiaki-unit                            # all suites
build/test/chiaki-unit /takion/av_packet_parse    # one test by full path (suite prefix + name)
```
Suites are registered in `test/main.c` (`/seq_num`, `/takion`, `/fec`, ...). Matching is prefix-based, so `/takion/av_packet_parse` also runs `..._real_video`.

**translator_core (Qt-free, fast loop, no GUI needed):**
```bash
cmake -S translator_core -B build-core -G Ninja -DTRANS_BUILD_TESTS=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
./build-core/translator_tests text_levenshtein    # filter by test-name substring (argv[1] in tests/TestMain.cpp)
```
Add `-DTRANS_ENABLE_ONNXRUNTIME=ON` to also build `translator_onnx_tests` and the `translator_ocr_probe` tool; those integration tests need the models in `assets/models/` (see `assets/models/README.md`).

**Packaging:** macOS translator build is `scripts/translator/build-macos.sh` (Homebrew deps listed at its top; output in `build-macos/`, models bundled into `chiaki.app/Contents/Resources/translator`). AppImage: `scripts/translator/build-appimage.sh` and `scripts/build-appimage.sh`. Flatpak manifests live in `scripts/flatpak/`.

PRs target `main`.

## Architecture

**`lib/` — C protocol core (`chiaki-lib`).** Discovery, session setup, the Takion transport, control channel, video/audio receive with FEC reassembly, crypto, and codec wrappers. Entry points for most protocol or media bugs: `lib/src/ctrl.c` (negotiation), `takion.c` (transport), `videoreceiver.c` / `audioreceiver.c`, `ffmpegdecoder.c` / `opus*.c`. Errors are returned as `ChiakiErrorCode`; there are no exceptions in C code, and every `*_init` has a matching `*_fini`. Protobuf is generated at build time from `lib/protobuf/takion.proto`; never commit generated `takion.pb.*`.

**`gui/` — Qt6/QML frontend** that wraps `lib/` through C++ bridge objects:
- `qmlbackend.cpp` is the QML-facing singleton: app state, discovery, PSN token refresh, session lifecycle.
- `streamsession.cpp` maps `Settings` plus host credentials into `ChiakiConnectInfo` and wires decoders, audio, haptics, and callbacks.
- `discoverymanager.cpp` turns `chiaki_discovery_*` callbacks into Qt signals.
- `qmlmainwindow.cpp` owns rendering: frames go through libplacebo, and QML overlays are composited on top.

Video path: `lib` receive → FFmpeg decode → `qmlbackend` `prepareFrameForPresentation` → `qmlmainwindow` render → QML. Input is the reverse: controller → `controllermanager` → `streamsession` → `lib` input sender. Behaviour is driven by `Settings`/`QmlSettings`; do not hardcode defaults in session code.

**`cli/`** is a thin wrapper (`discover`, `wakeup`), also reachable from GUI command mode via `gui/src/main.cpp`. **`android/`, `switch/`, `steamdeck_native/`, `setsu/`** are platform frontends or optional integrations, gated by `CHIAKI_ENABLE_*` / `CHIAKI_GUI_ENABLE_*` options.

**`translator_core/` — subtitle translator pipeline (C++17, must stay Qt-free).** Reads decoded frames, OCRs Japanese subtitles, and translates them. Pipeline:
1. `FrameProcessor`: copies only the ROI (10 Hz throttle), converts NV12/P010/etc. to RGB, waits for 250 ms of stable text.
2. `OcrEngine` (PP-OCRv3 det + rec through ONNX Runtime). Post-processing lives in `OcrPostprocess` and handles vertical columns without rotating the image.
3. `TextProcessor`: normalization, Levenshtein ≥ 0.9 dedup, LRU translation cache, 3-line context and prompts.
4. `TranslatorBackend`: async interface. Cloud backends (Gemini, DeepL) are in `gui/src/translator/cloudbackends.cpp`; optional offline NLLB is `NllbBackend.cpp`.
5. `DictLookup`: JMdict SQLite for tap-to-define.

`TranslatorPipeline` owns the worker thread and an `SpscRingBuffer<QueuedFrame, 4>`. When the ring is full it drops the new frame. It never blocks the decoder thread.

Qt-side adapter: `gui/src/translator/translatormanager.cpp` is the QML singleton, and `gui/src/qml/translator/TranslatorOverlay.qml` is loaded via a `Loader` in `StreamView.qml`. Threads: the Qt GUI thread owns QML, settings, dictionary tap, and `QNetworkAccessManager`; the FFmpeg frame thread calls `submitFrame`; the OCR worker does the heavy work; cloud replies come back on Qt's network thread and are queued to the GUI thread.

**Upstream hook points.** The translator is kept out of upstream code except for six small `CHIAKI_ENABLE_TRANSLATOR` hunks: `qmlbackend.cpp/.h` (create manager, `submitFrame` after `prepareFrameForPresentation`, `onSessionEnded`), `controllermanager.cpp/.h` (paddle buttons emit `PaddleButton(1..4)` instead of `return false`), `StreamView.qml` (Loader), `qml.qrc`, and the CMake files. New translator features that need upstream touchpoints should add to these hooks rather than spread edits across more upstream files. `docs/translator/ARCHITECTURE.md` (Thai) has the full rationale.

## Conventions and gotchas

- Many CMake options are tri-state (`AUTO|ON|OFF`, via `tri_option`), not booleans.
- Dependency bumps must be made consistently in `CMakeLists.txt`, `scripts/` (build and flatpak scripts), and `.github/workflows/`.
- Don't edit vendored trees (`third-party/`, `ffmpeg/`, `SDL2-*`, `sdl2-compat-*`, `switch/borealis/`) unless the task requires it.
- Keep platform code behind existing guards (`Q_OS_*`, `WIN32`, `CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE`, `CHIAKI_GUI_ENABLE_SETSU`).
- Unit coverage is thin. Changes to media, rendering, audio, or session timing can pass tests and still regress live playback, so say when a change needs a manual test against a console.
- QML: input and focus handlers belong on focusable `Item`s, not top-level `Window`s.
- Translator API keys are stored in plain-text `QSettings`. Never log keys or request bodies that contain them.
- `.github/copilot-instructions.md` and `CONTRIBUTOR_GUIDE.md` have more detail on the upstream build matrix and the code-reading order.
