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
optional, in-viewport Japanese subtitle translator for Steam Deck and desktop play.

## Extended features

| Area | What chiaki-ngx adds |
| --- | --- |
| OCR | PaddleOCR PP-OCRv3, RapidOCR PP-OCRv5, Google Cloud Vision, OCR.space, Gemini Vision, Chrome Screen AI (Linux x86_64) |
| Translation | Gemini, Google Translate (free), Google Cloud Translation, DeepL, OpenAI-compatible/OpenCode Go, and offline NLLB-200 |
| Context | Rolling 3-line dialogue context, LRU translation cache, Levenshtein duplicate suppression |
| Dictionary | Offline JMdict SQLite lookup with deinflection and tap-to-define |
| Overlay | Box-accurate multi-block in-place overlay, bottom fallback, configurable auto-hide, font family and size |
| Timing | Configurable scan interval and text-stability delay |
| Controls | Steam Deck L4/R4/L5/R5, touch routing, live enable/disable, remappable hotkeys with key capture |
| Packaging | Linux AppImage, macOS arm64 DMG, and upstream Flatpak/Windows build paths |
| Quality | 68 translator-core tests, overlay geometry/preference/shortcut tests, cloud/OCR mock tests, QML smoke checks, and `scripts/verify.sh --full` |

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

## Documentation

- [Translator overview](docs/translator/README.md)
- [Architecture](docs/translator/ARCHITECTURE.md)
- [OCR and translation providers](docs/translator/PROVIDERS.md)
- [Steam Deck](docs/translator/STEAMDECK.md) · [macOS](docs/translator/MACOS.md)
- [Roadmap](docs/translator/ROADMAP.md) · [Status](docs/translator/STATUS.md)

## Releases

Download the verified macOS DMG and Steam Deck AppImage from
[GitHub Releases](https://github.com/kidpeterpan/chiaki-ngx/releases).

## Credits and license

Based on the [upstream Chiaki Remote Play project](https://github.com/chiaki-ng/chiaki-ng) and the original
[Chiaki](https://github.com/thestr4ng3r/chiaki) project. The client is distributed under
AGPL-3.0-only-OpenSSL. OCR/dictionary assets retain their upstream licenses; see
[`assets/models/README.md`](assets/models/README.md) and
[`assets/dict/README.md`](assets/dict/README.md).

This project is not endorsed or certified by Sony Interactive Entertainment LLC.
