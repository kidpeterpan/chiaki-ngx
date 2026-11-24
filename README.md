
![chiaki-ngx Logo](gui/res/chiaking-logo.svg)

# [chiaki-ngx](https://chiaki-ng.github.io/chiaki-ng/)

An open source PlayStation remote play project serving as the next-generation of Chiaki with improvements and ongoing support now that the original Chiaki project is in maintenance mode only. [Click here to see the accompanying site for documentation, updates and more](https://chiaki-ng.github.io/chiaki-ng/).

## Discord
[chiaki-ngx community Discord](https://discord.gg/tAMbRuwXDH)

## Official downloads
Only download chiaki-ngx from these sources:

- [GitHub Releases](https://github.com/chiaki-ng/chiaki-ng/releases)
- [Flathub](https://flathub.org/apps/io.github.streetpea.Chiaki4deck)
- winget: `winget install --id=StreetPea.chiaki-ng -e`
- Homebrew: `brew install --cask streetpea/streetpea/chiaki-ng`

Other websites offering chiaki-ngx downloads, such as chiaki-ng.com, are not run by this project, and we can't vouch for the files they serve.

## Disclaimer
This project is not endorsed or certified by Sony Interactive Entertainment LLC.

Chiaki is a Free and Open Source Software Client for PlayStation 4 and PlayStation 5 Remote Play
for Linux, FreeBSD, OpenBSD, Android, macOS, Windows, Nintendo Switch and potentially even more platforms.

## Verifying the project

One command answers "is this tree healthy?" — it builds, tests, starts, and scans for
committed secrets. CI runs the same `--full` command on macOS, so a green local run means a
green pipeline.

```bash
scripts/verify.sh --quick            # ~5 s warm, while editing
scripts/verify.sh --full             # ~80 s, before opening a PR (the default)
scripts/verify.sh --stage security   # one stage plus its prerequisites
scripts/verify.sh --full --json      # machine-readable summary on stdout
```

`make verify`, `verify-quick`, `verify-json` and `verify-stage STAGE=<name>` wrap the same script.

| Stage | Checks | `--quick` |
| --- | --- | :-: |
| `env` | Toolchain and Homebrew formulas, submodules at their pinned commits, required assets tracked | ✓ |
| `lint` | shellcheck, actionlint, YAML syntax, qmllint, no leftover pre-rename project names | ✓ |
| `build-core` | `translator_core` compiles with warnings as errors | ✓ |
| `test-core` | `translator_core` unit tests (ctest) | ✓ |
| `build-release` | Clean Release build of the app with the translator and ONNX Runtime | |
| `test-unit` | `lib/` protocol tests (`chiaki-unit`) | |
| `smoke` | OCR and dictionary probes on tracked fixtures; app `--help` starts and writes no user settings | |
| `security` | gitleaks secret scan, osv-scanner audit (fails on HIGH/CRITICAL), no credential-shaped files tracked | |

Prerequisites (macOS with Homebrew; `env` reports whatever is missing):

```bash
brew install cmake ninja qt libplacebo ffmpeg onnxruntime miniupnpc json-c \
  openssl@3 opus sdl2-compat shellcheck actionlint gitleaks osv-scanner
```

Exit codes: `0` pass, `1` a stage failed, `2` a prerequisite is missing, `64` bad usage.
A failure prints the failing command and a capped excerpt; full per-stage logs land in
`.verify-logs/latest/`. [`AGENTS.md`](AGENTS.md) has the rules, including the only three
places an exception may be recorded, and [`scripts/verify/TRIAGE.md`](scripts/verify/TRIAGE.md)
tracks accepted and outstanding findings.

Deliberately not covered: a windowed GUI launch (it would mutate real app state) and network
isolation during tests. `TRIAGE.md` explains both.

## Subtitle translator fork (`translator_core/`)

This fork adds an optional, in-viewport Japanese subtitle translator for
PlayStation Remote Play: on-device OCR (PaddleOCR PP-OCRv3 via ONNX Runtime),
duplicate suppression, Gemini/DeepL translation with a three-line dialogue
context, an offline JMdict touch dictionary, and Steam Deck paddle shortcuts.

- Documentation: [`docs/translator/README.md`](docs/translator/README.md)
- Architecture and upstream hook points: [`docs/translator/ARCHITECTURE.md`](docs/translator/ARCHITECTURE.md)
- Roadmap and verification status: [`docs/translator/ROADMAP.md`](docs/translator/ROADMAP.md)
- Build: `-DCHIAKI_ENABLE_TRANSLATOR=ON -DTRANS_ENABLE_ONNXRUNTIME=ON`
- macOS (Homebrew dependencies): `scripts/translator/fetch-assets.sh` then `scripts/translator/build-macos.sh`, which builds `build-macos/gui/chiaki.app` with the models bundled (see [`docs/translator/MACOS.md`](docs/translator/MACOS.md))
- Optional offline translation: `-DTRANS_ENABLE_CTRANSLATE2=ON` plus an NLLB model (see `docs/translator/OFFLINE_NLLB.md`)
- Core tests without the GUI: `cmake -S translator_core -B build-core && cmake --build build-core && ctest --test-dir build-core`

The feature is opt-in at runtime and disabled by default; it does not touch
`lib/` and can be removed by turning the CMake option off.
