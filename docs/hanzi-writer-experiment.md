# Experiment: Hanzi Writer synthetic augmentation for Zinnia

**Date:** 2026-11-13  
**Status:** completed (experimental models only — not production)  
**Held-out note:** `samples-test/` is empty (0 files). No held-out contamination. All numbers below are on the **214-sample development set** (`samples/`). Treat them as optimistic until a true held-out set exists.

---

## 1. Dataset findings (brief)

| Source | What we found | Licence | Use in this experiment |
|--------|---------------|---------|------------------------|
| **Hanzi Writer data** ([chanind/hanzi-writer-data](https://github.com/chanind/hanzi-writer-data)) | ~9.5k JSON files; `medians` are ordered polylines; traditional + simplified present (國/国, 學/学, 龍/龙, …). Coords: 1024 box, y decreases downward. | **Arphic Public License** (derived from Make Me a Hanzi / Arphic fonts). Modifications must remain Freely Available under APL. | Yes — medians → Zinnia strokes. Experimental weights only. |
| **Make Me a Hanzi** | Upstream of Hanzi Writer graphics | Graphics: APL; dictionary: LGPL-3 | Cited for provenance |
| **Tegaki community (`tegaki-db`)** | Django app for contributing samples; **no redistributable multi-writer dump found** in the public repo | Project GPL-2; sample dump not available | Not used |
| **KanjiVG** | Japanese stroke order SVG | CC BY-SA 3.0 | Not used for training (eval corpus elsewhere) |

**Trained-model redistribution:** see detailed analysis in [`arphic-pl-model-distribution.md`](arphic-pl-model-distribution.md). Short version: HZW stroke data is APL-covered; whether Zinnia weights must ship under APL is unresolved — keep experimental models out of production downloads.

---

## 2. What was implemented

| Artifact | Path |
|----------|------|
| Converter + augmenter | `scripts/hanzi_writer_to_zinnia.py` |
| Offline list fusion (separate recognizers) | `scripts/fuse_candidate_dumps.py` |
| Hybrid latency breakdown | `build/hybrid_profile` (`src/profile_hybrid_main.cpp`) |
| Unit smoke test | `tests/hanzi_writer_convert_test.py` |
| Train/eval outputs | `experiments/hanzi-writer-aug/` (gitignored for large files) |
| Licence copy | `experiments/hanzi-writer-aug/ARPHICPL.TXT` |
| `zinnia_learn` / `zinnia_convert` | Built from vendored sources (`build/zinnia_learn`; also added to `CMakeLists.txt`) |

Pipeline: Hanzi Writer JSON medians → coordinate map to 0–1000 Tomoe space → mild augmentation → Zinnia S-expression lines → `zinnia_learn`.

Augmentation (seed `20261113`): wobble amp 8, jitter 2, scale 0.85–1.15, aspect 0.9–1.1, rotate ±5°, resample step 12. Stroke count/order preserved.

---

## 3. Training configs

Full Tegaki Japanese inventory retained in every model (so competitors stay fair). Extra Hanzi Writer lines added as below. Production weights were **not** replaced.

Overlap Tegaki∩Hanzi Writer: **5,029** characters (selection by Unicode code point when `--max-chars 0`; no eval-set preference).

### 3a. Full-overlap, matched example counts (primary)

Exactly **one** HZW example per overlap character (5,029 HZW lines each). Same seed `20261113`.

| Tag | HZW content | Train lines | Train time | JA `.model` size |
|-----|-------------|------------:|----------:|-----------------:|
| production | — | — | — | 26.1 MB |
| **full-clean** | 1 clean median × 5029 | 11,675 | ~341 s | **30.8 MB** |
| **full-aug** | 1 augmented median × 5029 | 11,675 | ~369 s | **31.6 MB** |

InkNet weights (unchanged): **24.6 MB**. Production TW model (unchanged): **52.2 MB**.

### 3b. Earlier pilots (for context)

| Tag | Training data | Lines | Train time | JA size |
|-----|---------------|------:|----------:|--------:|
| baseline retrain | Tegaki only | 6,646 | ~177 s | 25 MB |
| sample-prefer 800 | Tegaki + 1 clean + 4 aug on 800 chars preferred from `samples/` | 10,646 | ~327 s | 26 MB |
| random800 | Tegaki + HZW on 800 random overlap chars | 10,646 | ~339 s | ~26 MB |

### 3c. Clean + extra aug (same 5,029 overlap)

Still Tegaki + full overlap; each char keeps **one clean median**, then **N** mild-aug copies (`--hzw-mode clean+aug`, `--variants N`).

| Tag | HZW content | Train lines | Train time | JA `.model` size |
|-----|-------------|------------:|----------:|-----------------:|
| **full-clean** (N=0) | 1 clean × 5029 | 11,675 | ~341 s | 30.8 MB |
| **full-clean1** (N=1) | 1 clean + 1 aug × 5029 | 16,704 | ~564 s | 31.8 MB |
| **full-clean2** (N=2) | 1 clean + 2 aug × 5029 | 21,733 | ~747 s | 32.3 MB |
| **hzw-only-clean** | 1 clean × 5029, **no Tegaki** | 5,029 | ~127 s | 23.6 MB |

`hzw-only-clean` exists only to compare **fused training** (Tegaki+HWZ in one `.model`) against **separate recognizers** (production Tegaki ∥ HZW-only, lists merged offline via `scripts/fuse_candidate_dumps.py`).

---

## 4. Benchmark results

Preprocess: `resample:4+smooth:3@merge+split`. Hybrid: `--neural ml/runs/base2/model.inknet --neural-mode hybrid --tol-penalty 0.1`. Dev set: 214 samples. Full overlap covers **190/214** sample files (169/190 unique chars in Tegaki∩HWZ).

### 4a. Full-overlap Zinnia (matched counts)

| Model | Mode | top-1 | top-5 | top-10 | ms/sample |
|-------|------|------:|------:|-------:|----------:|
| Production | ja | 63.6 | 78.5 | 82.2 | 51 |
| Production | both | 63.6 | 82.7 | 85.0 | 149 |
| **full-clean** | ja | **71.0** | 84.1 | 85.5 | 55 |
| **full-clean** | both | **71.0** | 85.5 | 86.4 | 152 |
| full-aug | ja | 67.3 | **85.5** | 86.4 | 57 |
| full-aug | both | 67.3 | 85.0 | **87.9** | 154 |

**Clean vs aug (same #examples):** clean wins top-1 by **+3.7** (ja); aug is slightly better on some top-5/top-10 cells. Mild geometric noise alone does not beat a faithful median when counts are matched.

**full-clean vs production (ja):** top-1 **+7.4**, top-5 **+5.6**, top-10 **+3.3**, latency **+4 ms**.

### 4b. Clean + extra aug series

| Model | Mode | top-1 | top-5 | top-10 | ms/sample |
|-------|------|------:|------:|-------:|----------:|
| full-clean (N=0) | ja | 71.0 | 84.1 | 85.5 | 55 |
| full-clean1 (N=1) | ja | **72.0** | **85.0** | 85.5 | 56 |
| full-clean2 (N=2) | ja | **72.0** | 84.1 | 86.0 | 57 |
| full-clean1 | both | **72.0** | 85.0 | **87.4** | 152 |
| full-clean2 | both | **72.0** | **86.0** | **87.4** | 152 |

One extra aug copy per overlap char gains **+1.0** top-1 on Zinnia alone; a second copy does not move top-1 further on this set. Latency stays ~same.

### 4c. Hybrid (InkNet + Zinnia)

| Zinnia half | Mode | top-1 | top-5 | top-10 | ms/sample |
|-------------|------|------:|------:|-------:|----------:|
| Production | both | 81.3 | 92.5 | 93.5 | 435 |
| **full-clean** | both | **83.6** | **94.9** | 94.9 | 437 |
| full-clean1 | both | 82.7 | 94.9 | **95.3** | 433 |
| full-clean2 | both | 83.2 | 94.9 | **95.3** | 434 |
| full-aug | both | 82.2 | 94.9 | **95.3** | 438 |

Extra aug that helped Zinnia alone **did not** improve hybrid top-1 vs clean-only. Best fair stack remains **full-clean + InkNet** (**83.6 / 94.9**).

### 4d. Fused training vs separate recognizers

**Fused** = one Zinnia model trained on Tegaki+HWZ (`full-clean`). **Separate** = production JA dump + `hzw-only-clean` dump, lists merged offline (`fuse_candidate_dumps.py`). Same preprocess; dumps use top-N 30.

| Setup | top-1 | top-5 | top-10 | MRR |
|-------|------:|------:|-------:|----:|
| Production alone (ja) | 63.6 | 78.5 | 82.2 | 0.700 |
| HZW-only alone (ja) | 53.7 | 67.8 | 73.8 | 0.602 |
| Separate fuse `interleave` | 63.6 | 84.1 | 86.4 | 0.733 |
| Separate fuse `rrf` | 62.6 | 82.2 | 86.4 | 0.701 |
| Separate fuse `max` | 65.9 | 84.1 | 86.4 | 0.739 |
| Separate fuse `z` | **68.7** | **84.6** | **86.9** | **0.759** |
| **Fused `full-clean` (ja)** | **71.0** | 84.1 | 85.5 | 0.769 |

Best list fusion (`z`) still trails fused training by **~2.3** top-1. Training Tegaki and HZW into one model beats keeping them as two recognizers and merging ranks afterward. (Separate shipping can still matter for APL packaging — see [`arphic-pl-model-distribution.md`](arphic-pl-model-distribution.md) — but accuracy prefers fused weights.)

### 4e. Hybrid latency profile (`hybrid_profile`)

Mean ms/sample over 214 real samples, **Release build** (`build-rel`, M1 Pro; weights = `base2`). Parts are timed separately with one reading each; `hybrid e2e` is the full `Recognizer` hybrid path, which also runs Zinnia on every merge/split reading, so it is larger than the sum of the parts.

| Models | prepare | neural render+infer | Zinnia JA | Zinnia TW | hybrid e2e |
|--------|--------:|--------------------:|----------:|----------:|-----------:|
| Production | 0.02 | 59.4 | 9.7 | 18.7 | 161.5 |
| full-clean | 0.03 | 59.4 | 10.8 | 18.7 | 163.8 |
| full-clean2 | 0.03 | 59.4 | 11.1 | 18.8 | 165.3 |

Logs: `experiments/hanzi-writer-aug/profile.hybrid.*.release.log`. An earlier version of this table was measured on an unoptimised build (InkNet about 282 ms of about 430 ms e2e) and wrongly concluded that InkNet dominates; in Release it is roughly a third of e2e, and the repeated Zinnia passes over the tolerance readings are a comparable share. Swapping JA to a larger HZW-augmented model adds about 1 ms to Zinnia JA and 2-4 ms e2e, so latency is not what decides between these models.

### 4f. Earlier pilot numbers (eval-dependent selection — not fair for shipping claims)

| Model | Mode | top-1 | top-5 | top-10 | ms |
|-------|------|------:|------:|-------:|---:|
| sample-prefer 800 Zinnia | ja | 76.2 | 86.0 | 88.3 | 54 |
| sample-prefer 800 + InkNet | both | 85.0 | 95.3 | 95.8 | 436 |
| random800 Zinnia | ja | 64.0 | 79.9 | 82.2 | 53 |

Those large sample-prefer gains came from concentrating extra templates on the eval alphabet; random800 ≈ production. Full-overlap is the honest scale-up.

### Caveats

0. Every number in this document comes from the same 214 dev samples (one writer) that were also used to choose configurations, so differences of a few points are not established. Nothing here should be quoted as a result until the held-out set (#18) has been run once.
1. Dev-set only (`samples-test/` still empty).
2. Real trackpad ink never entered training.
3. TW production model unchanged (symlink in experimental dirs).
4. Do not ship HZW-augmented weights until APL review ([`arphic-pl-model-distribution.md`](arphic-pl-model-distribution.md)).

---

## 5. Reproduction commands (full-overlap)

```bash
git clone --depth 1 https://github.com/chanind/hanzi-writer-data.git experiments/hanzi-writer-data
./scripts/fetch_models.sh

python3 tests/hanzi_writer_convert_test.py

# Matched-count pair (no --prefer-sample-chars)
python3 scripts/hanzi_writer_to_zinnia.py \
  --hanzi-dir experiments/hanzi-writer-data/data \
  --tegaki-xml models_dl/tegaki-zinnia-japanese-0.3/handwriting-ja.xml \
  --out experiments/hanzi-writer-aug/train.ja.full-clean.s \
  --max-chars 0 --hzw-mode clean --seed 20261113

python3 scripts/hanzi_writer_to_zinnia.py \
  --hanzi-dir experiments/hanzi-writer-data/data \
  --tegaki-xml models_dl/tegaki-zinnia-japanese-0.3/handwriting-ja.xml \
  --out experiments/hanzi-writer-aug/train.ja.full-aug.s \
  --max-chars 0 --hzw-mode aug --seed 20261113

./build/zinnia_learn experiments/hanzi-writer-aug/train.ja.full-clean.s \
  experiments/hanzi-writer-aug/ja.full-clean.model
./build/zinnia_learn experiments/hanzi-writer-aug/train.ja.full-aug.s \
  experiments/hanzi-writer-aug/ja.full-aug.model

# Install experimental JA model into a private models dir (keep TW as production symlink)
./build/handwriting_eval --samples samples \
  --models experiments/hanzi-writer-aug/models-full-clean \
  --modes ja,both --variants 'resample:4+smooth:3@merge+split'

./build/handwriting_eval --samples samples \
  --models experiments/hanzi-writer-aug/models-full-clean \
  --modes both --variants 'resample:4+smooth:3@merge+split' \
  --neural ml/runs/base2/model.inknet --neural-mode hybrid --tol-penalty 0.1

# Clean + 1/2 aug extras
python3 scripts/hanzi_writer_to_zinnia.py \
  --hanzi-dir experiments/hanzi-writer-data/data \
  --tegaki-xml models_dl/tegaki-zinnia-japanese-0.3/handwriting-ja.xml \
  --out experiments/hanzi-writer-aug/train.ja.full-clean1.s \
  --max-chars 0 --hzw-mode clean+aug --variants 1 --seed 20261113

# HZW-only (for separate-recognizer fuse)
python3 scripts/hanzi_writer_to_zinnia.py \
  --hanzi-dir experiments/hanzi-writer-data/data \
  --tegaki-xml models_dl/tegaki-zinnia-japanese-0.3/handwriting-ja.xml \
  --out experiments/hanzi-writer-aug/train.ja.hzw-only-clean.s \
  --max-chars 0 --hzw-mode clean --no-tegaki --seed 20261113

./build/handwriting_eval --samples samples --models experiments/hanzi-writer-aug/models-production \
  --modes ja --variants 'resample:4+smooth:3@merge+split' \
  --dump experiments/hanzi-writer-aug/dump.production.ja.json
./build/handwriting_eval --samples samples --models experiments/hanzi-writer-aug/models-hzw-only-clean \
  --modes ja --variants 'resample:4+smooth:3@merge+split' \
  --dump experiments/hanzi-writer-aug/dump.hzw-only-clean.ja.json
python3 scripts/fuse_candidate_dumps.py \
  --a experiments/hanzi-writer-aug/dump.production.ja.json \
  --b experiments/hanzi-writer-aug/dump.hzw-only-clean.ja.json

./build/hybrid_profile --samples samples \
  --models experiments/hanzi-writer-aug/models-full-clean \
  --neural ml/runs/base2/model.inknet
```

---

## 6. Recommendation

1. **Do not replace production models.** Gains are real on the dev set but held-out is empty and APL on weights is unresolved.
2. **Prefer fused Tegaki+HWZ training** over separate recognizers + list fusion (fused **71.0** vs best fuse **68.7** top-1). Separate models remain an APL packaging option, not an accuracy win.
3. **Prefer clean medians over mild aug-only** at matched N; **one extra aug copy** (+1 top-1 Zinnia) is worth trying, but **hybrid still prefers clean-only** on this set.
4. **Latency:** hybrid e2e is about 165 ms in a Release build (InkNet about 59 ms); larger JA models add only a few ms — size/licence, not speed, constrain shipping.
5. **Best eval-independent stack so far:** full-clean Zinnia + existing InkNet hybrid (**83.6 / 94.9** top-1/top-5).
6. **Next:** fill `samples-test/`; counsel review of [`arphic-pl-model-distribution.md`](arphic-pl-model-distribution.md) before any public model tarball.

### Bottom line

Full 5,029-character overlap, no eval-dependent selection: **fused clean medians beat aug-only and beat separate list fusion**; **one extra aug helps Zinnia a little but not hybrid**; **latency is similar across these models (about 165 ms hybrid, Release)**. Production downloads stay Tegaki-only.
