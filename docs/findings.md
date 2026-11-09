# Handwriting prototype findings

**Status:** macOS arm64 build + headless smoke succeeded. Interactive Phase D benchmark not yet completed.

## Build / smoke

| Check | Result |
|-------|--------|
| Zinnia builds on macOS arm64 | Yes (AppleClang, Qt 6.12) |
| Japanese model loads | Yes (`handwriting-ja.model`, ~25 MB) |
| Traditional Chinese model loads | Yes (`handwriting-zh_TW.model`, ~50 MB) |
| Headless smoke candidates printed | Yes — both models + Both mode |

Hardcoded Zinnia SDK sample strokes (9 strokes) returned ranked UTF-8 candidates. Example top hits: JA `#1 春`, TW `#1 酋` / `#2 春`. Both mode interleaved with model provenance. Latencies on this machine were ~11 ms (JA), ~22 ms (TW), ~38 ms (Both) for that sample.

Linux build not yet verified on this workstation (same CMake project; install Qt6 + cmake per README).

## Recognition quality (to fill in)

Metrics: exact hit at rank 1 / top 5 / top 10; related-variant hit recorded separately.

| Category | n | top-1 exact | top-5 exact | top-10 exact | related variant in top-10 | notes |
|----------|---|-------------|-------------|--------------|---------------------------|-------|
| Simple | | | | | | |
| Common Japanese | | | | | | |
| Traditional forms | | | | | | |
| Variant pairs | | | | | | |
| Similar shapes | | | | | | |
| Complex | | | | | | |
| Historical / rare | | | | | | |

Soft floor for common characters: ≥85% top-5 exact. Traditional-form usefulness is the scholarly gate.

Seed list: [`tests/samples.json`](../tests/samples.json).

## Decision

_Pending Phase D interactive testing with the GUI (`./build/handwriting_app`)._

Question to answer: Is Zinnia/Tegaki good enough as marinaMoji’s first handwriting backend, or should we investigate a modern neural alternative?


## Synthetic preprocessing results (offline)

Measured with `handwriting_eval --synthetic` on 15 simple kanji. Smoothing of dense noisy ink gave a large gain; RDP at epsilon <= 8 is a no-op (Zinnia simplifies at about 9.5px itself). Larger crop margins and RDP epsilon 12 looked better but the corpus is simple characters only, so they were deliberately not adopted. Default is `resample:4+smooth:3`. Re-tune only with real saved samples, including complex characters.

## KanjiVG synthetic evaluation (2026-11-09)

Ink generated from KanjiVG stroke paths (CC BY-SA 3.0, fetched by `scripts/fetch_kanjivg.sh`, never committed) with the synthetic distortion model, run through the shared `Recognizer`. Japanese-form ground truth only; wobble/jitter parameters are guesses, so treat differences under about 2 points as noise (800 samples gives roughly +/-1.3).

**Fit and preprocessing.** On 600 random characters, recognising the raw surface ink gave ja top-1 69.0. Crop/fit raised it to 78.2, and resample+smooth to 80.0. A sweep of 10 preprocessing variants on 800 characters (resample 3-6, smooth 2-5, RDP 6-12) stayed within 82.0-83.5 top-1, so the default `resample:4+smooth:3` sits on a plateau. RDP 12 reads +1.5 top-1 but earlier analysis suggests it hurts complex characters, so it was not adopted. Retune on real samples.

**Both-mode fusion.** Compared interleave, reciprocal rank fusion, max score, per-list z-score and per-model bias on 300 random characters and on 51 traditional-form characters (x5). All score-based methods landed within noise of interleave (for example traditional forms, top-1: ja 86.7, tw 82.0, interleave 86.7, rrf 87.5, z 86.7). RRF lowers top-1 on the random set (64.3 vs 81.7) because it breaks ties unpredictably. Interleave stays the default; the `fusion` options in `Recognizer` remain available. A real decision needs traditional-form samples written by hand (issue #9), since synthetic Japanese-form ink cannot show a difference.

**Tooling.** `scripts/check_eval_gate.py` enforces `tests/eval_thresholds.json` in CI. CI numbers differ slightly from local ones because the C++ standard libraries implement the random distributions differently; the thresholds sit several points below both.

## Real samples and stroke-order/count tolerance (2026-11-09, issue #13)

First 206 real trackpad samples (dev set, one writer). Compared with KanjiVG's stroke data, 55 of them differ in stroke order or count (`scripts/check_stroke_order.py`). The recognizer handles those badly: Japanese-model top-1 was 67% for matching samples, 37% when the order differed and 22% when the count differed. Real writing is also below the synthetic benchmark (about 80%) even when the order matches.

Tolerance tries extra readings of the ink and keeps each character's best score: **merge** joins two consecutive strokes whose gap is small, **split** cuts a stroke at its sharpest corner, **swap** exchanges adjacent strokes. Extra readings are penalised by 0.1 so the original wins ties.

Tuned on characters with an even code point (half A, 102 samples), checked on odd (half B, 112, untouched while tuning). Japanese model, baseline -> merge+split:

| Half | top-1 | top-5 | top-10 |
|---|---|---|---|
| A (tuned) | 56.9 -> 63.7 | 67.6 -> 79.4 | 75.5 -> 83.3 |
| B (held out) | 57.1 -> 62.5 | 65.2 -> 75.9 | 69.6 -> 79.5 |
| B, Both mode | 57.1 -> 62.5 | 69.6 -> 80.4 | 72.3 -> 84.8 |
| B, matching-order samples only | 64.4 -> 72.2 | 72.2 -> 83.3 | 76.7 -> 84.4 |

- Without a penalty, clean samples lost 2-12 points of top-1 because wrong readings sometimes outscored the right one. A penalty of 0.1 removes that; larger penalties lose the gains.
- Swap adds a few points of top-10 but costs 4x the latency (about 220 ms Japanese, 645 ms Both) and was not adopted.
- Latency of the default (merge+split): about 55 ms Japanese, 160 ms Both, run in the background.
- No regression on the KanjiVG synthetic set (top-1 71.4 -> 71.0, top-5 +5).
- Caveats: one writer, 206 samples, so this is a measured improvement but still small-sample. The held-out set (#18) is the real check. Defaults can be changed with `HANDWRITING_PREPROCESS` (a spec without `@` disables tolerance, for example `resample:4+smooth:3`).
