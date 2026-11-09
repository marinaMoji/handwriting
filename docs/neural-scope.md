# Scope: order-free neural recognizer (issue #16)

Status: scoping only, nothing built. Written 2026-11-09 after the stroke-order work (#13).

## Why consider it

Measured on 214 real dev samples (docs/findings.md): Zinnia's top-1 is 67% when stroke order and count match KanjiVG, 37% when the order differs and 22% when the count differs. Tolerance tricks (merge/split, count prior) bring the total to 67% top-1 / 81% top-5 (Japanese model), 84% top-5 in Both mode, just under the 85% soft floor. The remaining errors are look-alike characters and writing that Zinnia's order-dependent features cannot absorb. A model that sees the ink as a picture ignores order and count by construction.

## Key fact found while scoping: Zinnia's training data is tiny

The model tarballs we already download (`models_dl/`) contain the training data:

| File | Samples | Distinct characters | Samples per character |
|---|---|---|---|
| `handwriting-ja.xml` | 6,646 | 6,421 | 1 (max 4) |
| `handwriting-zh_TW.xml` | 11,853 | 11,853 | exactly 1 |

(Tomoe Handwriting Dictionary format, coordinates 0-1000, same LGPL-2.1 license as the models.) So Zinnia is effectively a one-shot recognizer from clean templates, with no augmentation. A neural model trained on these same templates plus heavy augmentation is not disadvantaged on data, and the data is already in-repo-adjacent and license-aligned. This is the main reason to expect it can work.

## Design

**Input.** Render the (cropped, fitted) ink to 64x64 as 9 channels: 1 for ink, and 8 for stroke direction (each segment drawn into the channel of its 8 quantised directions, the classic "directional feature map" trick). Stroke order and count are not represented. Direction is, which separates look-alikes that differ in stroke direction.

**Model.** Small ResNet-style CNN, about 2-4M parameters, softmax over the union of the ja and zh_TW character sets (12,890 classes, the same set Zinnia can output and the Unihan table covers).

**Training data.** Generated on the fly from the Tegaki XMLs. Augmentation reuses the distortion model from `SyntheticCorpus.h` (wobble, jitter, scale, rotation, translation) plus: stroke width, speed-independent resampling, random stroke merges and splits (same as the tolerance readings), per-stroke offsets, stray specks, occasional dropped or doubled stroke segments. Evaluation never uses training templates: decisions are made on real samples only.

**Use.** Three modes behind one option: Zinnia (today), neural, hybrid (reciprocal-rank or log-probability fusion of both lists; weight tuned on dev). Hybrid is the likely winner: Zinnia is better when ink is clean and in order, the network when it is not.

**Rejected:** sequence models (LSTM/transformer over strokes). They reintroduce order dependence, the problem we are trying to remove.

## What this machine can do

- Apple M1 Pro, 10 cores, 16 GB RAM, Metal (MPS) available to PyTorch.
- Python 3.13 with numpy and PIL; **no PyTorch, ONNX or Core ML installed**. Training needs PyTorch (about 1-2 GB with dependencies). I would put it in a repo-local virtualenv (`.venv-ml/`, gitignored) so the conda base is untouched. This needs your OK before I install anything.
- Disk: 29 GB free at 97% usage. Enough (training data is generated on the fly, checkpoints are tens of MB), but tight.
- Compute estimate (unmeasured, assume +/-2x): a 64x64, ~3M-parameter net on MPS should process a few thousand images per second. 12.9k classes x 50 augmented renders is about 650k images per "epoch", roughly 5-10 minutes. A useful baseline is probably 2-6 hours of training. I will measure throughput first, before committing to a schedule.

## Shipping options (decide only if the prototype wins)

1. **ONNX Runtime C++**: cross-platform, well supported, about 10-20 MB extra dependency, needs CMake integration on three OSes.
2. **Hand-written C++ inference**: a small CNN is a few hundred lines; zero dependencies, fully portable, weights in a binary file; must be made fast enough and checked against PyTorch outputs (fixture test in CI). More work but fits the project's "pure, testable core" style.
3. **Core ML / DirectML per platform**: fastest per platform, but three codepaths. Not recommended.

Plan: prototype and evaluate purely offline in Python first (neural scores computed per sample and fed to the existing C++ eval for hybrid fusion), so no runtime decision is needed until we know it helps.

## Licensing (to verify, not legal advice)

- Training data: Tegaki XMLs, LGPL-2.1, same as the models we already use. Network weights trained from them are a derivative; we would ship them with the same notices as the Zinnia models.
- KanjiVG (CC BY-SA 3.0) is not needed as a training source and I would avoid it, to avoid adding share-alike terms to the weights. It remains useful as an independent evaluation corpus.
- Unihan counts (Unicode License v3) are already in use.

## Evaluation protocol

- Decide on the 214 real dev samples through the existing tooling: whole set, flagged/clean groups, and the even/odd character halves. Use the held-out set (#18) once for the final report.
- KanjiVG synthetic ink is an independent corpus and a regression check; it is not decisive (it is optimistic about real writing).
- Metrics: top-1/5/10, MRR, latency, and the model size on disk.

## Risks

1. **Augmentation realism.** With one template per class, the network learns only what our augmentation shows it. If real trackpad ink differs from our noise model, accuracy on real samples will disappoint however good the synthetic numbers are. Mitigation: look at augmented renders next to your real samples, and iterate against real-sample results only.
2. **Rare and traditional characters** have one template each, the same as for Zinnia, so no worse, but not better either.
3. **Look-alike pairs** (大/丈, 己/巳) differ in tiny details; 64x64 may blur them. A 96x96 input is the fallback.
4. **No non-BMP characters**: same limit as Zinnia's models.
5. **Latency and size**: expected small (tens of ms, tens of MB); to be measured.
6. **Cross-platform runtime** is the riskiest engineering part if it works; deferred by the offline-first plan.

## Plan and decision gates

| # | Step | Effort | Needs you |
|---|---|---|---|
| M0 | Create `.venv-ml`, install PyTorch, confirm MPS and measure throughput | 30 min | OK to install (~1-2 GB) |
| M1 | Data pipeline: parse the Tegaki XMLs, 9-channel renderer, augmentation, unit tests, contact sheet of augmented vs real samples | half a day | glance at the contact sheet |
| M2 | Train a baseline on ja + zh_TW (12,890 classes), offline-evaluate on dev samples and KanjiVG synthetic | few hours of training | nothing |
| M3 | Up to 3 iterations on augmentation and architecture, judged on real dev samples only | a day | nothing |
| M4 | Hybrid fusion in the eval tool; **decision gate** | 2-3 hours | nothing |
| M5 | If go: runtime choice, C++ inference, `Recognizer` integration behind an option, CI fixture, three-OS build | days | testing on Linux/Windows |

**Go / no-go at M4:** proceed only if hybrid beats today's Both mode on real dev samples by at least 5 points top-5 (or lifts the flagged group clearly) without hurting the clean group, and the held-out set agrees. Otherwise stop and record the result; the Zinnia pipeline stays as is.

## Honest expectation

I cannot promise a win. The reasons it should work (order-free, direction-aware, augmentation Zinnia never had, same data class as Zinnia) are good, but the main risk (augmentation realism) can only be tested by trying. The offline-first plan caps the cost of finding out at roughly M0-M4: about two days of my work, one decision from you now (the install) and a few minutes of glancing at images.
