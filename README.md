```text
      ___           ___                       ___           ___                       ___           ___           ___
     /\  \         /\__\          ___        /\  \         /\__\          ___        /\__\         /\  \         |\__\
    /::\  \       /:/  /         /\  \      /::\  \       /:/  /         /\  \      /::|  |       /::\  \        |:|  |
   /:/\:\  \     /:/__/          \:\  \    /:/\:\  \     /:/__/          \:\  \    /:|:|  |      /:/\:\  \       |:|  |
  /:/  \:\  \   /::\  \ ___      /::\__\  /::\~\:\  \   /::\__\____      /::\__\  /:/|:|  |__   /:/  \:\  \      |:|__|__
 /:/__/ \:\__\ /:/\:\  /\__\  __/:/\/__/ /:/\:\ \:\__\ /:/\:::::\__\  __/:/\/__/ /:/ |:| /\__\ /:/__/_\:\__\ ____/::::\__\
 \:\  \  \/__/ \/__\:\/:/  / /\/:/  /    \/__\:\/:/  / \/_|:|~~|~    /\/:/  /    \/__|:|/:/  / \:\  /\ \/__/ \::::/~~/~
  \:\  \            \::/  /  \::/__/          \::/  /     |:|  |     \::/__/         |:/:/  /   \:\ \:\__\    ~~|:|~~|
   \:\  \           /:/  /    \:\__\          /:/  /      |:|  |      \:\__\         |::/  /     \:\/:/  /      |:|  |
    \:\__\         /:/  /      \/__/         /:/  /       |:|  |       \/__/         /:/  /       \::/  /       |:|  |
     \/__/         \/__/                     \/__/         \|__|                     \/__/         \/__/         \|__|
```

**chiaki-ngx** is a fork of the [upstream Chiaki Remote Play project](https://github.com/chiaki-ng/chiaki-ng), the
open-source PlayStation 4/5 Remote Play client. It keeps the upstream client and adds an
optional, in-viewport Japanese subtitle translator with multiple OCR engines and translation
providers. Translate Japanese subtitles into Thai or English.

Website: [chiaki-ngx](https://kidpeterpan.github.io/chiaki-ngx/)

## Extended features

| Area | What chiaki-ngx adds |
| --- | --- |
| OCR | PaddleOCR PP-OCRv3, RapidOCR PP-OCRv5, Google Cloud Vision, OCR.space, Gemini Vision, Chrome Screen AI (Linux x86_64) |
| Translation | Google Translate (free, the default: no API key needed), Gemini, Google Cloud Translation, DeepL, OpenAI-compatible/OpenCode Go, and offline NLLB-200 |
| Language flow | Japanese subtitles → Thai or English |
| Context | Rolling 3-line dialogue context, LRU translation cache, Levenshtein duplicate suppression |
| Dictionary | Offline JMdict SQLite lookup with deinflection and tap-to-define |
| Overlay | Box-accurate multi-block in-place overlay, bottom fallback, configurable auto-hide, font family and size, original line Show/Hide (Hide gives the translation the whole box), box opacity 20–100% |
| Timing | Configurable scan interval and text-stability delay; stability is judged on text pixels, so a scene moving behind a see-through dialogue box does not block live translation, and a slow OCR fallback (every 1.5 s, backing off to 5 s, none for cloud OCR) still reads an area that never settles |
| Settings | Steam Deck controller navigation (D-pad moves, left/right changes, A selects, B closes, L1/R1 switch sections), a setup checklist that names what is missing and how to fix it, settings applied when the panel closes |
| Controls | Steam Deck L4/R4/L5/R5, touch routing, a Live switch (LIVE: translates on its own; MANUAL: only L4 / Translate now; OFF) on the badge, F11 and the stream menu, remappable hotkeys with key capture |
| Packaging | Steam Deck / Linux x86_64 AppImage with curl 8.23 on a static OpenSSL 3, plus upstream Flatpak/Windows build paths |
| Quality | translator-core, cloud backend, cloud OCR, shortcut, preference, overlay geometry and controller-driven QML settings tests, run by `scripts/verify.sh --full` (see [Verification](#verification)); a Chrome Screen AI restart test joins them on Linux once the library is fetched |

The translator is opt-in and disabled by default. Without it, chiaki-ngx behaves like the
upstream client.

## Build

```bash
git clone --recurse-submodules https://github.com/kidpeterpan/chiaki-ngx.git
cd chiaki-ngx

cmake -S . -B build -G Ninja \
  -DCHIAKI_ENABLE_TRANSLATOR=ON \
  -DTRANS_ENABLE_ONNXRUNTIME=ON
cmake --build build --target chiaki
```

- **macOS:** `scripts/translator/fetch-assets.sh && scripts/translator/build-macos.sh`
- **Offline NLLB:** add `-DTRANS_ENABLE_CTRANSLATE2=ON` and provide a model
- **Core tests:** `cmake -S translator_core -B build-core -DTRANS_BUILD_TESTS=ON && ctest --test-dir build-core`
- **Full verification:** `scripts/verify.sh --full`

## Verification

`scripts/verify.sh` is the single health check, run locally and in CI (`.github/workflows/verify.yml`):

| Stage | Checks |
| --- | --- |
| `env` | Toolchain, libraries and submodules for the stages being run |
| `lint` | ShellCheck, actionlint, YAML, qmllint, and leftover pre-rename project names |
| `build-core`, `test-core` | `translator_core` with warnings as errors, and its tests |
| `build-release` | The shipping app in Release |
| `test-unit` | The `lib/` protocol unit tests |
| `test-gui` | Every translator suite under `gui/tests/translator/`, including the controller-driven settings panel |
| `smoke` | OCR and dictionary probes on fixtures, and the app's `--help` without touching user settings |
| `security` | Credential-shaped files, gitleaks, and an osv-scanner audit that fails on HIGH/CRITICAL (rated by CVSS when an advisory has no label) |

- `--quick` runs `env lint build-core test-core` for the inner loop; `--stage NAME` runs one stage and its
  prerequisites; `--json` prints a machine-readable summary.
- Runs are locked, time-boxed per stage and logged under `.verify-logs/`; exit codes and rules are in
  [`AGENTS.md`](AGENTS.md), known gaps in [`scripts/verify/TRIAGE.md`](scripts/verify/TRIAGE.md).
- `scripts/verify/selftest.sh` tests the harness itself (exit codes, skips, timeouts, the lock,
  interrupts, JSON, log pruning) with stand-in stages; CI runs it before the full verification.

## Documentation

- [Translator overview](docs/translator/README.md)
- [Architecture](docs/translator/ARCHITECTURE.md)
- [OCR and translation providers](docs/translator/PROVIDERS.md)
- [Steam Deck](docs/translator/STEAMDECK.md) · [macOS](docs/translator/MACOS.md)
- [Roadmap](docs/translator/ROADMAP.md) · [Status](docs/translator/STATUS.md)

## Releases

Download the verified Steam Deck AppImage from
[GitHub Releases](https://github.com/kidpeterpan/chiaki-ngx/releases). Versions start at
`v0.1.0-translator`; the earlier v0.2.x releases were withdrawn (Chrome Screen AI could close the
app on a settings change, and the bundled curl had HIGH/CRITICAL CVEs). macOS DMG builds are no
longer published; build from source with the [macOS guide](docs/translator/MACOS.md).

## Credits and license

Based on the [upstream Chiaki Remote Play project](https://github.com/chiaki-ng/chiaki-ng) and the original
[Chiaki](https://github.com/thestr4ng3r/chiaki) project. The client is distributed under
AGPL-3.0-only-OpenSSL. OCR/dictionary assets retain their upstream licenses; see
[`assets/models/README.md`](assets/models/README.md) and
[`assets/dict/README.md`](assets/dict/README.md).

This project is not endorsed or certified by Sony Interactive Entertainment LLC.
