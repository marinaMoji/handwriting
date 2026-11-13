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
- `handwriting_app` — drawing UI with JA / TW / Both modes; Both mode uses the Zinnia + InkNet hybrid when the InkNet weights are present
- `stroke_mapping_test` — pad→canvas mapping, crop/fit, jump detection, hold-key matching
- `trackpad_source_test` — finger/palm tracking shared by all trackpad backends
- `ptp_frame_test` — Windows Precision Touchpad frame assembly
- `handwriting_eval` — replay saved/synthetic ink and measure accuracy; `handwriting_recognize` — recognize saved samples from the command line
- `recognizer_test`, `fusion_test`, `kanjivg_test`, `stroke_variants_test`, `stroke_preprocess_test`, `sample_io_test`, `mt_slot_tracker_test` — see Tests below

## Run

### macOS / Linux

```bash
./build/handwriting_app
# optional: pass a models directory and/or InkNet weights
./build/handwriting_app /path/to/models --inknet /path/to/inknet.bin

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

### Hybrid recognition (Zinnia + InkNet)

In **Both** mode the app mixes Zinnia (stroke order matters) with InkNet, a small order-free neural net, which is much better at ink whose stroke order or count differs from the templates: on the 214 development samples (one writer) top-1 / top-5 go from 66.8 / 84.1 to 81.3 / 92.1. Latency is about 110 to 160 ms per recognition in a Release build on an Apple M-series laptop; the build defaults to Release for this reason (an unoptimised build is about four times slower). The status bar and diagnostics line say which engine answered. Japanese-only and Traditional-only modes use Zinnia alone. Details: [docs/neural-scope.md](docs/neural-scope.md).

The InkNet weights (24.6 MB, trained on the Tegaki templates only, not committed) are looked up in this order: `--inknet FILE`, `$HANDWRITING_INKNET`, `MODELS_DIR/inknet.bin`, `data/inknet.bin`. Get them with

```bash
scripts/fetch_inknet.sh ml/runs/base2/model.inknet     # copy from a machine that has them (sha256 checked)
INKNET_URL=https://... scripts/fetch_inknet.sh         # or download, once a location exists
```

or train your own (see [ml/README.md](ml/README.md)). Without weights the app runs on Zinnia alone and says so in the status bar. `--no-neural` or `HANDWRITING_NEURAL=off` turns the hybrid off, for comparison.

### Alternative Japanese model

`--ja-alt FILE`, `$HANDWRITING_JA_ALT` or `MODELS_DIR/handwriting-ja-alt.model` loads a second Japanese Zinnia model next to the default; an **Alt. JA model** checkbox then switches between them in the window (the hybrid keeps working with either). The default never changes by itself. The staged Hanzi Writer-enhanced model of [docs/licensing/handwriting/](docs/licensing/handwriting/README.md) is meant to be tried this way, for local testing only until the legal review (issue #21) is resolved:

```bash
./build/handwriting_app --ja-alt experiments/hzw-package/stage/marinamoji-handwriting-ja-hzw-experimental/model/handwriting-ja-hzw-experimental.model
```

### Collecting samples

**Writer…** records who is writing and **Practice** prompts characters and saves them with that metadata; see [docs/sample-protocol.md](docs/sample-protocol.md).

### Quick start

1. Launch the app and choose Japanese, Traditional Chinese, or Both.
2. Draw with click-drag, **or** hold the key left of **1** and write on the trackpad (see below).
3. Pick a candidate and press Copy.

Troubleshooting: the status bar explains when the trackpad could not be used (for example a Linux user missing from the `input` group) and the app falls back to cursor-driven writing. Set `HANDWRITING_DEBUG_KEYS=1` to log key events (to find your hold key) and `HANDWRITING_DEBUG_TOUCH=1` to log raw touch contacts.

## Drawing input

- **Click-drag** (mouse or trackpad click): press, draw a stroke, release. Repeat for each stroke.
- **Hold-to-write** (trackpad-friendly): hold **F12** (for writing with the left hand, so the right hand can hold the key; on Mac laptops it needs **fn** unless function keys are set to standard) or the key at the top-left of the main block (left of **1**: `` ` `` on US/UK, **²** on French AZERTY, **§** on many Nordic/Swiss Mac layouts), then write with your finger on the trackpad. The physical key is matched by hardware code (macOS key code, Windows scan code `0x29`, X11/Linux keycode 49), so it works on any layout. It reads absolute finger positions from the trackpad (public **NSTouch** API on macOS, evdev on Linux, Raw Input on Windows), and learns the real trackpad dimensions from the device: the whole trackpad maps to a pad-shaped drawing area (no dead zones, no stretching). Write anywhere at any size: on recognition the ink is cropped to its bounding box and fitted to the 300×300 recognizer canvas. Lift between strokes. `HANDWRITING_PAD_ASPECT` forces a pad aspect (default: read from the device, 1.6 until the first touch). If that path is unavailable, cursor movement is used as a fallback. Release the key when finished (switching apps also ends hold). The key is ignored while typing in a text field. Click-drag still works for mouse users.

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

## Evaluation and tests

### Collecting real samples

Type the character you meant in the **Expected** box and press Return (or *Save sample*). Ink is stored as JSON in `samples/` (override with `HANDWRITING_SAMPLES_DIR`; the directories are gitignored). The plan, character lists and dev/test/textbook sets are in [docs/sample-protocol.md](docs/sample-protocol.md).

```bash
python3 scripts/sample_coverage.py              # progress per category, what is missing
python3 scripts/check_stroke_order.py           # flag samples whose stroke order/count differs from KanjiVG
```

### Replaying and measuring

```bash
./build/handwriting_eval --samples samples --models models --modes ja,tw,both \
    --variants "raw,none,resample:4+smooth:3,resample:4+smooth:3@merge+split" --misses
./build/handwriting_recognize --mode both --top 5 samples/U+5B66_*.json     # one-off recognition of saved samples
```

`handwriting_eval` prints top-1/5/10, MRR and latency per variant and mode. A variant is a preprocessing spec optionally followed by `@swap`, `@merge`, `@split` (stroke-order/count tolerance, see below); `raw` skips crop/fit and preprocessing. Useful options: `--misses`, `--csv FILE`, `--fusions interleave,rrf,max,z,cal` (Both-mode merging), `--tol-penalty P`, `--count-weight W --count-band B`, `--char-split 0|1` (tune on even code points, check on odd), `--keep-specks`.

### Synthetic corpus from KanjiVG

`scripts/fetch_kanjivg.sh` downloads [KanjiVG](https://kanjivg.tagaini.net) (CC BY-SA 3.0; kept in the gitignored `data/`, never redistributed). With it, `handwriting_eval` generates distorted ink for thousands of characters:

```bash
bash scripts/fetch_kanjivg.sh
./build/handwriting_eval --models models --kanjivg data/kanjivg.xml --kvg-max 300 --modes ja,tw,both
```

CI runs this on 300 characters (seed 1234) and fails if accuracy drops below `tests/eval_thresholds.json` (`scripts/check_eval_gate.py`). Raise the thresholds deliberately when accuracy improves. Synthetic ink is good for catching regressions; it is optimistic about real handwriting, so tune on real samples.

### Recognition pipeline

One shared path (`Recognizer`, used by the app, `handwriting_eval` and `handwriting_recognize`):

1. Drop specks (no-length taps, edge/palm contacts; also dropped at capture).
2. Crop to the ink and fit the 300x300 recognizer canvas.
3. Preprocess: resample + smooth. Override with `HANDWRITING_PREPROCESS`, e.g. `none`, `rdp:3`, `smooth:2`.
4. Run the model(s). The default spec `resample:4+smooth:3@merge+split+count` also tries merged and split stroke readings (stroke-order/count tolerance), keeps each character's best score, and adds a soft stroke-count prior (candidates whose expected stroke count is more than 1 away from the written count are penalised). A spec without `@` turns all of that off; `@swap` adds adjacent-stroke swaps (slower, not worth it so far).
5. In Both mode, merge the Japanese and Traditional lists (default: interleave).

Measurements and caveats are in [docs/findings.md](docs/findings.md).

### Tests

`ctest --test-dir build` runs: stroke mapping, trackpad source, PTP frame assembly, stroke preprocessing, stroke variants, sample I/O, stroke counts, the Linux slot tracker (`MtSlotTracker`, testable on any OS), `Recognizer`, fusion, KanjiVG parsing, and (on Linux) the evdev fixture below. CI (`.github/workflows/ci.yml`) builds and tests on macOS, Ubuntu and Windows, then runs the synthetic accuracy gate.

- **Linux evdev fixture** (issue #4): `evdev_uinput_test` creates a virtual type-B pad via `/dev/uinput`, plays scripted strokes (including palm handoff, `SYN_DROPPED`, unplug/re-probe) and checks `EvdevTrackpadSource`. Needs write access to `/dev/uinput`; without it the test is **skipped** (exit 77) so CI stays green. On a VM/machine:

  ```bash
  # one-shot for a local run (or install a udev rule for persistent access)
  sudo chmod a+rw /dev/uinput
  ctest --test-dir build -R evdev_uinput_test --output-on-failure
  # optional: pin a node when debugging a real pad
  # HANDWRITING_EVDEV_DEVICE=/dev/input/event8 ./build/handwriting_app
  ```

### Stroke counts (third-party data)

`src/StrokeCountTable.inc` holds expected stroke counts for the characters the two Zinnia models can output, derived from the Unihan `kTotalStrokes` field (Unicode 18.0.0, (c) Unicode, Inc., [Unicode License v3](https://www.unicode.org/license.txt)). Regenerate with `bash scripts/fetch_unihan.sh && python3 scripts/gen_stroke_counts.py` (needs the built `dump_charset` target and the models).

## Out of scope

IME integration, dictionaries, OpenCC / variant reranking.

## Decision notes

After interactive testing, record results in [docs/findings.md](docs/findings.md). Seed characters for a smoke corpus are in [tests/samples.json](tests/samples.json).
