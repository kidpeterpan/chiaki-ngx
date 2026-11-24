
![chiaki-ng Logo](gui/res/chiaking-logo.svg)

# [chiaki-ng](https://chiaki-ng.github.io/chiaki-ng/)

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://chiaki-ng.github.io/chiaki-ng/).

## Discord
[chiaki-ng community Discord](https://discord.gg/tAMbRuwXDH)

## Official downloads
Only download chiaki-ng from these sources:

- [GitHub Releases](https://github.com/chiaki-ng/chiaki-ng/releases)
- [Flathub](https://flathub.org/apps/io.github.streetpea.Chiaki4deck)
- winget: `winget install --id=StreetPea.chiaki-ng -e`
- Homebrew: `brew install --cask streetpea/streetpea/chiaki-ng`

Other websites offering chiaki-ng downloads, such as chiaki-ng.com, are not run by this project, and we can't vouch for the files they serve.

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.

## Subtitle translator fork (`translator_core/`)

This fork adds an optional, in-viewport Japanese subtitle translator for
PlayStation Remote Play: on-device OCR (PaddleOCR PP-OCRv3 via ONNX Runtime),
duplicate suppression, Gemini/DeepL translation with a three-line dialogue
context, an offline JMdict touch dictionary, and Steam Deck paddle shortcuts.

- Documentation: [`docs/translator/README.md`](docs/translator/README.md)
- Architecture and upstream hook points: [`docs/translator/ARCHITECTURE.md`](docs/translator/ARCHITECTURE.md)
- Roadmap and verification status: [`docs/translator/ROADMAP.md`](docs/translator/ROADMAP.md)
- Build: `-DCHIAKI_ENABLE_TRANSLATOR=ON -DTRANS_ENABLE_ONNXRUNTIME=ON`
- Optional offline translation: `-DTRANS_ENABLE_CTRANSLATE2=ON` plus an NLLB model (see `docs/translator/OFFLINE_NLLB.md`)
- Core tests without the GUI: `cmake -S translator_core -B build-core && cmake --build build-core && ctest --test-dir build-core`

The feature is opt-in at runtime and disabled by default; it does not touch
`lib/` and can be removed by turning the CMake option off.
