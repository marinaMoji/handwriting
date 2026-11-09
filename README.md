# marinaMoji handwriting prototype

Standalone Qt 6 + Zinnia experiment for Japanese and Traditional Chinese handwriting recognition (Tegaki models). Separate from the marinaMoji IME — used to decide whether Zinnia is good enough to integrate later.

**Platforms:** macOS, Linux and Windows. macOS is the tested path; the Linux and Windows trackpad backends are implemented but have not been run on real hardware yet (see [Trackpad backends](#trackpad-backends)). Click-drag drawing works everywhere.

## Dependencies

- CMake ≥ 3.16
- C++17 compiler
- Qt 6 (Widgets, Concurrent)
- curl and tar (model download)

### macOS (Homebrew)

```bash
brew install cmake qt
```

### Linux

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake curl qt6-base-dev
# Fedora
sudo dnf install gcc-c++ cmake curl qt6-qtbase-devel
# Arch
sudo pacman -S base-devel cmake curl qt6-base
```

Package names vary by release; you need the Qt 6 Widgets and Concurrent development files.

For absolute trackpad writing, your user must be able to read `/dev/input/event*`, normally by joining the `input` group (log out and back in afterwards):

```bash
sudo usermod -aG input "$USER"
```

### Windows

- [Visual Studio 2022](https://visualstudio.microsoft.com/) (or Build Tools) with the **Desktop development with C++** workload and the Windows SDK (provides the HID headers and `hid.lib`)
- [CMake](https://cmake.org/download/)
- [Qt 6](https://www.qt.io/download-qt-installer) for MSVC (e.g. `C:\Qt\6.7.0\msvc2019_64`)
- Git for Windows (provides Git Bash, used for the model download script). `curl.exe` and `tar.exe` already ship with Windows 10/11.

## Get models

macOS / Linux:

```bash
./scripts/fetch_models.sh
```

Windows: run the same script from **Git Bash** (`bash scripts/fetch_models.sh`), or WSL.

See [models/README.md](models/README.md) for licenses and mirrors.

## Build

### macOS

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
```

### Linux

```bash
cmake -S . -B build        # add -DCMAKE_PREFIX_PATH=/path/to/Qt6 if Qt is not found
cmake --build build -j
```

### Windows (PowerShell or Developer Command Prompt)

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.7.0\msvc2019_64"
cmake --build build --config Release
# Copy the Qt DLLs next to the executable (once):
C:\Qt\6.7.0\msvc2019_64\bin\windeployqt.exe build\Release\handwriting_app.exe
```

Targets:

- `handwriting_smoke` — headless recognition of a hardcoded stroke sample
- `handwriting_app` — drawing UI with JA / TW / Both modes
- `stroke_mapping_test` — pad→canvas mapping, crop/fit, jump detection, hold-key matching
- `trackpad_source_test` — finger/palm tracking shared by all trackpad backends
- `ptp_frame_test` — Windows Precision Touchpad frame assembly

## Run

### macOS / Linux

```bash
./build/handwriting_app
# optional: pass a models directory
./build/handwriting_app /path/to/models

# checks
./build/handwriting_smoke
./build/stroke_mapping_test && ./build/trackpad_source_test && ./build/ptp_frame_test
```

### Windows

```powershell
.\build\Release\handwriting_app.exe
# optional: pass a models directory
.\build\Release\handwriting_app.exe C:\path\to\models

# checks
.\build\Release\handwriting_smoke.exe
.\build\Release\stroke_mapping_test.exe
```

(With a single-config generator such as Ninja, the executables are directly in `build\`.)

### Quick start

1. Launch the app and choose Japanese, Traditional Chinese, or Both.
2. Draw with click-drag, **or** hold the key left of **1** and write on the trackpad (see below).
3. Pick a candidate and press Copy.

Troubleshooting: the status bar explains when the trackpad could not be used (for example a Linux user missing from the `input` group) and the app falls back to cursor-driven writing. Set `HANDWRITING_DEBUG_KEYS=1` to log key events (to find your hold key) and `HANDWRITING_DEBUG_TOUCH=1` to log raw touch contacts.

## Drawing input

- **Click-drag** (mouse or trackpad click): press, draw a stroke, release. Repeat for each stroke.
- **Hold-to-write** (trackpad-friendly): hold the key at the top-left of the main block (left of **1**: `` ` `` on US/UK, **²** on French AZERTY, **§** on many Nordic/Swiss Mac layouts), then write with your finger on the trackpad. The physical key is matched by hardware code (macOS key code, Windows scan code `0x29`, X11/Linux keycode 49), so it works on any layout. It reads absolute finger positions from the trackpad (public **NSTouch** API on macOS, evdev on Linux, Raw Input on Windows), and learns the real trackpad dimensions from the device: the whole trackpad maps to a pad-shaped drawing area (no dead zones, no stretching). Write anywhere at any size: on recognition the ink is cropped to its bounding box and fitted to the 300×300 recognizer canvas. Lift between strokes. `HANDWRITING_PAD_ASPECT` forces a pad aspect (default: read from the device, 1.6 until the first touch). If that path is unavailable, cursor movement is used as a fallback. Release the key when finished (switching apps also ends hold). The key is ignored while typing in a text field. Click-drag still works for mouse users.

## Trackpad backends

All absolute backends feed the same [`TrackpadSource`](src/TrackpadSource.h) stroke state machine (`processContacts`). Choose with `HANDWRITING_CAPTURE`:

| Value | Platform | Notes |
|-------|----------|--------|
| `nstouch` (default on macOS) | macOS | Public AppKit `NSTouch` / gesture monitor |
| `evdev` (default on Linux) | Linux | Multitouch type B via `/dev/input/event*` — user usually needs the `input` group |
| `rawinput` (default on Windows) | Windows | HID Digitizer / Precision Touch Pad via Raw Input + `HidP_*` |
| `qt` | all | Portable Qt `TouchPad` events (often unavailable on macOS trackpads) |

If the preferred native backend is missing or cannot open the device, the factory falls back to `qt-touch`, then to cursor hold-to-write. Click-drag always works.

## Distribution note

macOS absolute input uses public AppKit APIs only. Linux needs device-node permissions (`input` group). Windows Raw Input + HID parse is best-effort across PTP vendors; treat it as a prototype until tested on target hardware. A shipping IME integration still needs a front-end that fits each platform's text-input framework.

## What this tests

- Mouse/trackpad stroke capture (click-drag and hold-to-write)
- Japanese and Traditional Chinese Zinnia models
- Dual-model “Both” mode with provenance (no cross-model score calibration)
- Latency and candidate ranking for a later go/no-go decision

## Out of scope

IME integration, dictionaries, OpenCC / variant reranking.

## Decision notes

After interactive testing, record results in [docs/findings.md](docs/findings.md). Seed characters for a smoke corpus are in [tests/samples.json](tests/samples.json).
