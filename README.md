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
- `stroke_mapping_test` — unit tests for pad→canvas mapping and jump detection

## Run

```bash
./build/handwriting_smoke
./build/stroke_mapping_test
./build/handwriting_app
# optional: pass a models directory
./build/handwriting_app /path/to/models
```

## Drawing input

- **Click-drag** (mouse or trackpad click): press, draw a stroke, release. Repeat for each stroke.
- **Hold-to-write** (trackpad-friendly): hold the key at the top-left of the main block (left of **1**: `` ` `` on US/UK, **²** on French AZERTY, **§** on many Nordic/Swiss Mac layouts), then write with your finger on the trackpad. On macOS the physical key is matched by hardware key code, so it works on any layout. It uses **MultitouchSupport** absolute finger positions: the whole trackpad maps to a pad-shaped drawing area (no dead zones, no stretching). Write anywhere at any size: on recognition the ink is cropped to its bounding box and fitted to the 300×300 recognizer canvas. If your pad is not ~1.6:1, set `HANDWRITING_PAD_ASPECT`; lift between strokes. If that path is unavailable, cursor movement is used as a fallback. Release the key when finished (Cmd-Tab / app deactivate also ends hold). The key is ignored while typing in a text field. Click-drag still works for mouse users.

## Distribution note (macOS)

Absolute trackpad input depends on Apple’s private **MultitouchSupport** framework. That is fine for this local prototype, but it **rules out App Store / standard notarized distribution** of a build that links or `dlopen`s it. A shipping IME integration would need a public API path, user-space fallback (click-drag only), or a different recognition front-end.

## What this tests

- Mouse/trackpad stroke capture (click-drag and hold-to-write)
- Japanese and Traditional Chinese Zinnia models
- Dual-model “Both” mode with provenance (no cross-model score calibration)
- Latency and candidate ranking for a later go/no-go decision

## Out of scope

IME integration, dictionaries, OpenCC / variant reranking, Windows builds.

## Decision notes

After interactive testing, record results in [docs/findings.md](docs/findings.md). Seed characters for a smoke corpus are in [tests/samples.json](tests/samples.json).
