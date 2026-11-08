# marinaMoji handwriting prototype

Standalone Qt 6 + Zinnia experiment for Japanese and Traditional Chinese handwriting recognition (Tegaki models). Separate from the marinaMoji IME — used to decide whether Zinnia is good enough to integrate later.

**Platforms:** Linux and macOS.

## Dependencies

- CMake ≥ 3.16
- C++17 compiler
- Qt 6 Widgets + Concurrent
- curl (for model download)

### macOS (Homebrew)

```bash
brew install cmake qt
```

### Linux (Debian/Ubuntu example)

```bash
sudo apt install build-essential cmake qt6-base-dev libqt6concurrent6t64
# Package names vary by release; ensure Qt6 Widgets + Concurrent are available.
```

## Get models

```bash
./scripts/fetch_models.sh
```

See [models/README.md](models/README.md) for licenses and mirrors.

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"   # macOS; omit PREFIX on many Linux distros
cmake --build build
```

Targets:

- `handwriting_smoke` — headless recognition of a hardcoded stroke sample
- `handwriting_app` — drawing UI with JA / TW / Both modes

## Run

```bash
./build/handwriting_smoke
./build/handwriting_app
# optional: pass a models directory
./build/handwriting_app /path/to/models
```

## Drawing input

- **Click-drag** (mouse or trackpad click): press, draw a stroke, release. Repeat for each stroke.
- **Hold-to-write** (trackpad-friendly): hold **Space**, then write with your finger on the trackpad. On macOS this uses **MultitouchSupport** absolute finger positions (trackpad corners = canvas corners; lift between strokes). If that path is unavailable, cursor movement is used as a fallback. Release Space when finished. Click-drag still works for mouse users.

## What this tests

- Mouse/trackpad stroke capture (click-drag and hold-to-write)
- Japanese and Traditional Chinese Zinnia models
- Dual-model “Both” mode with provenance (no cross-model score calibration)
- Latency and candidate ranking for a later go/no-go decision

## Out of scope

IME integration, dictionaries, OpenCC / variant reranking, Windows builds.

## Decision notes

After interactive testing, record results in [docs/findings.md](docs/findings.md). Seed characters for a smoke corpus are in [tests/samples.json](tests/samples.json).
