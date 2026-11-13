# Handwriting datasets and training plan

**Status:** research only — no large-scale training started.  
**Date:** 2026-11-13  
**Scope:** improve mouse/trackpad recognition for Japanese, traditional Chinese, and historical forms, without putting restricted corpora into CI or release artifacts.

This note verifies datasets against original project docs where possible, inventories what marinaMoji already has, and proposes the smallest useful next experiment.

---

## 1. Dataset comparison

| Source | Format | Size / coverage | Trajectory quality | Licence | Commercial / redistribute trained model | Fit for marinaMoji |
|--------|--------|-----------------|--------------------|---------|-------------------------------------------|--------------------|
| **CASIA-OLHWDB** (official) | Online strokes in `.pot` (x,y sequences, stroke ends) | ~3.9M isolated chars; 7,185 Chinese + 171 symbols; 1,020 writers | Genuine human online ink; GB encodings | Academic research agreement; CAS retains copyright; commercial use charged ([conditions](https://nlpr.ia.ac.cn/databases/handwriting/Application_form.html)) | **Unresolved / likely prohibited** without commercial licence | Strong research corpus for *simplified* GB2312; weak for kyūjitai / Japanese; **must stay outside release** |
| **HF AlienKevin/CASIA-OLHWDB1.0-1.2** | Viewer columns: `image` (PNG bytes), `character`, `writer_id` | Claims ~3.74M rows | **Not stroke trajectories** in the published HF schema — raster images | Same CASIA academic terms (card copies them) | Same restrictions | **Do not use for Zinnia training.** Not a drop-in online corpus |
| **WritingRing** | IMU ring (200 Hz) + Sensel Morph touchpad `.gz` | ~20 writers, ~20 h; Latin letters/words | Touchpad X/Y + pressure exist; Latin script only | **Unspecified** on HF (empty metadata) | Unknown | Domain-adjacent for *pad noise*; not a CJK training set |
| **OnHW** | IMU pen sensors; some reconstructed tip trajectories | Latin chars, words, equations | Sensor-first; not kanji | OnHW: **CC BY-NC 4.0**; ICROW subset has extra “no training” rule | NC blocks open-source shipping | Variability / DA research only |
| **Calligraphy Bench** | Reference images + SVG path strings + median polylines | 88 hard characters × master calligraphers | Synthetic/reference calligraphy paths, not trackpad human writing | Unclear on card (research benchmark) | Unclear | Not a training set for mouse/pad ink |
| **BCSS** | 400×400 images + per-stroke binary masks | ~1.2k packaged images | Image segmentation; **no stroke order/timing** | **MIT** (stated for research) | Permitted with attribution | Weak for online Zinnia; optional stroke-mask ideas only |
| **Tegaki / Tomoe XMLs** | Tomoe XML strokes (0–1000) | ja ≈ 6.4k chars (~1 sample each); zh_TW ≈ 11.9k (1 each); zh_CN also exists | Clean templates, not multi-writer | **LGPL-2.1** | Redistribution OK under LGPL | **Already integrated**; best licensed training base |
| **KanjiVG** | SVG paths + stroke order | Large kanji inventory | Idealized vectors; synthetic when sampled | **CC BY-SA 3.0** | Share-alike may attach to derivatives | Keep for **evaluation / synthetic regression**, not shipped training weights |
| **marinaMoji samples** | JSON strokes, hold-to-write / click-drag | Dev ~214 (one writer); held-out planned | Exact target domain (trackpad) | Contributor consent (to be formalized) | Only if consent covers redistribution | Best domain adaptation / benchmark data |

### CASIA notes (priority A)

- Official **online** data stores concatenations of `(x,y)` with stroke end `(-1,0)` and character end `(-1,-1)` in `.pot` files ([Online Database](https://nlpr.ia.ac.cn/databases/handwriting/Online_database.html)). Labels are GB codes, not Unicode — conversion and normalization are required.
- Character set is **GB2312-oriented simplified Chinese**, not traditional/historical Japanese forms. Overlap with marinaMoji’s TW/kyūjitai needs is partial at best.
- Academic terms: research use by the applicant group only; no commercial use of samples; CAS retains copyright; cite Liu et al., ICDAR 2011. **Permission to train ≠ permission to distribute a trained model** in an open-source IME.
- The Hugging Face mirror’s dataset viewer shows PNG signatures (`89 50 4E 47…`) under an `image` column. Treat that mirror as **offline images**, not as a verified online-stroke dump. Prefer official `.pot` only, and only in a private research sandbox.

### Secondary corpora (B–E)

- **WritingRing:** useful to study continuous pad trajectories and pressure; tasks are English letters/words; licence must be clarified before any dependency.
- **OnHW:** Latin + NC licence — do not mix into redistributable models.
- **Calligraphy Bench:** SVG/medians for calligraphy *agent* eval; not human mouse/trackpad ink.
- **BCSS:** stroke *masks* on calligraphy images; cannot recover writing order or timing.

---

## 2. Existing model assessment

### What is already in the prototype

| Piece | Location / notes |
|-------|------------------|
| Zinnia engine | Vendored under `third_party/zinnia-src` (BSD-style) |
| Japanese model | `handwriting-ja.model` via `scripts/fetch_models.sh` (Tegaki 0.3, LGPL-2.1) |
| Traditional Chinese model | `handwriting-zh_TW.model` (same) |
| Training templates | Inside download tarballs: `handwriting-ja.xml`, `handwriting-zh_TW.xml` (Tomoe format) |
| Simplified Chinese Tegaki model | Exists upstream (`tegaki-zinnia-simplified-chinese`) — **not fetched or loaded today** |
| Capture | Mouse click-drag; hold-to-write via `TrackpadSource` (macOS NSTouch, Linux evdev, Windows Raw Input) |
| Normalization | Pad→surface absolute map; crop/fit (`StrokeMapping.h`); resample/smooth; speck drop; merge/split tolerance; Unihan stroke-count prior |
| Ranking | Zinnia scores; JA/TW interleave (Both); optional neural hybrid (`InkNet`) |
| Eval | `handwriting_eval`, CI gates, KanjiVG synthetic corpus, real `samples/` protocol |
| Neural path | Offline-trained CNN on Tegaki templates + augmentation; hybrid improves order-mismatched ink (see `docs/neural-scope.md`, `docs/findings.md`) |

### What is missing

- Multi-writer, domain-matched (mouse/trackpad) training data with clear redistribution rights.
- Systematic coverage of kyūjitai / rare forms beyond single Tomoe templates.
- Simplified Chinese model only if product scope expands (marinaMoji focus is JA + traditional/historical).
- A **shipping** model trained on large external CJK online corpora without a commercial licence path.
- Ghost Canvas is out of scope for this investigation (per constraints); capture/normalize/recognize already exist in the prototype.

### Current limitations (measured)

On ~214 real trackpad samples (one writer), after merge/split + stroke-count prior (`docs/findings.md`):

- Japanese top-1 / top-5 ≈ **67% / 81%**; Both top-5 ≈ **84%** (just under 85% soft floor).
- Stroke-order/count mismatches collapse Zinnia (≈37% / 22% top-1); neural/hybrid helps those cases.
- Remaining errors: look-alikes and ranking (top-5 ≫ top-1), plus weak traditional/rare coverage.

Zinnia’s training data is essentially **one clean template per character** — so accuracy on messy pad ink is limited by design, not only by missing “big data.”

---

## 3. Benchmark design

Reuse existing tooling; do not invent a parallel eval stack.

**Test corpora**

| Corpus | Role | Rule |
|--------|------|------|
| `samples/` | Dev / tuning | May inspect |
| `samples-test/` | Held-out (#18) | Report once; never tune on |
| `samples-textbook/` | Stroke-order control (#19) | Optional |
| KanjiVG synthetic | Regression / synthetic upper bound | Not decisive for shipping |
| Optional private CASIA subset | Research-only comparison | Never in CI/release |

**Metrics** (already largely supported by `handwriting_eval`)

- Top-1 / top-5 / top-10 exact; MRR optional.
- Latency (p50/p95) per mode (ja / tw / both; neural off/hybrid).
- Stratify by stroke count, shinjitai vs kyūjitai, rare/historical (`docs/sample-chars.json` categories).
- Separate mouse click-drag vs hold-to-write (`source` field in sample JSON).
- Failure tags: capture / normalize / coverage (char absent) / ranking (in list but not #1).

**Reproducibility**

- Pin model hashes, preprocess spec (`HANDWRITING_PREPROCESS`), neural weight path, and eval command in a short “run card.”
- Keep restricted data paths outside the git tree (e.g. `~/research/casia/`, gitignored).

---

## 4. Training feasibility

```text
Online handwriting datasets
          |
          v
Format-specific parsers  (.pot / Tomoe XML / marinaMoji JSON)
          |
          v
Unicode / label validation  (GB→Unicode; NFC; reject junk)
          |
          v
Stroke normalization  (fit box, resample; optional pad-noise model)
          |
          v
Training corpus  (provenance tags; licence tier: ship vs research-only)
          |
          v
Zinnia train  and/or  neural (InkNet) train
          |
          v
Benchmark vs frozen baseline on samples-test
```

**Zinnia compatibility:** Tomoe/Tegaki XML → `zinnia_learn` is the native path (already how Tegaki models were built). CASIA `.pot` → Tomoe XML is straightforward mechanically; licence is the blocker for shipping.

**Separate vs combined models:** Keep **separate JA and TW** recognizers (current design). A combined SVM/CNN over 12k+ classes is already explored for neural hybrid; do not merge Zinnia models into one blob without a clear win on held-out data.

**Compute:** Zinnia retrain on Tomoe-scale data is modest (minutes–hours on a laptop). CASIA-scale SVM training is heavier and still not shippable. Neural baseline already ~1–2 h on Apple MPS for Tegaki-aug data.

---

## 5. Recommended strategy

Ranked by expected improvement × effort × legal risk (low risk first):

1. **Freeze and report the held-out baseline** (`samples-test`) with today’s Zinnia + optional hybrid — no new data. Establishes the number everything else must beat.
2. **Ship-safe data scale-up:** opt-in marinaMoji trackpad/mouse corpus (Dec 2026 workshops) + keep Tegaki LGPL templates. Domain match is highest; legal path is clearest.
3. **Improve augmentation / hybrid** on Tegaki-only training (already started) — attacks order noise without new restricted corpora.
4. **Optional research-only CASIA `.pot` sandbox** (never in release): prove converter + measure ceiling on GB2312 online ink. Informs whether “more writers” helps *in principle*.
5. **Avoid for shipping:** HF CASIA images, OnHW (NC), WritingRing (unclear licence + Latin), BCSS/Calligraphy Bench as primary trainers, KanjiVG as training source (BY-SA).

### Best dataset for an *initial* experiment

**Tegaki Tomoe XMLs already on disk after `fetch_models.sh`, plus a small slice of marinaMoji real samples used only for evaluation (not training).**

Why:

- Already Zinnia-native; LGPL-aligned with current models.
- Matches product languages better than CASIA’s simplified GB set.
- Zero new legal surface for a redistributable prototype.
- CASIA remains valuable later as a **private research ceiling**, not as experiment #1 for an OSS IME.

If the question is “largest real multi-writer online Chinese ink,” the answer is **official CASIA-OLHWDB `.pot`**, but only under academic terms and **not** for distributed marinaMoji models until a commercial licence (or a different corpus) exists.

---

## 6. Implementation roadmap (small stages)

| Stage | Deliverable | Exit criterion |
|-------|-------------|----------------|
| **R0** | This report + licence register (table above) | Done |
| **R1** | Held-out baseline card: top-1/5/10, latency, strata, mouse vs pad | Numbers checked into `docs/` (no model change) |
| **R2** | Consent + sample schema v2 (device, timestamps optional, consent id, dataset version) | Protocol signed off for workshops |
| **R3** | Workshop collection (20–30 people × 50–100 chars) → private archive | Coverage report via `sample_coverage.py` |
| **R4** | Ship-safe experiment: Tegaki XML ± consented samples → Zinnia *or* neural retrain → compare to R1 on held-out | ≥5 pt top-5 gain **or** clear negative result recorded |
| **R5** | Optional private CASIA `.pot` parser + research-only model (gitignored) | Converter tests; **no** CI/release coupling |
| **R6** | Only if R4 wins: packaging, LGPL notices, app default unchanged until gate | Production swap behind flag |

Do **not** download CASIA into the repo, CI caches, or model tarballs.

---

## 7. Native marinaMoji dataset (proposal)

Opt-in only; workshops Dec 2026 as first drive.

Suggested fields (extends current JSON):

- `character` (Unicode)
- `strokes` (ordered point lists) + explicit stroke boundaries (already list-of-strokes)
- `source`: `hold-to-write` | `click-drag` | …
- `device`: `trackpad` | `mouse` | `stylus` (new)
- `timestamps` optional per point or per stroke
- `contributor_id` anonymous, only with consent
- `consent_version`, `dataset_version`
- `note` free text

Target: 20–30 volunteers × 50–100 characters → roughly 1–3k samples: enough for **benchmarking and light domain adaptation**, not for replacing Tegaki from scratch.

---

## 8. Licence register (summary)

| Field | CASIA | WritingRing | OnHW | Calligraphy Bench | BCSS | Tegaki models/XML | KanjiVG | Own samples |
|-------|-------|-------------|------|-------------------|------|-------------------|---------|-------------|
| Source | NLPR/CAS | CHI’25 / HF dBHz | Fraunhofer IIS | Haize Labs | Stroke-Seg / HF | tegaki.github.io | kanjivg.tagaini.net | marinaMoji |
| Format | `.pot` strokes / HF PNGs | IMU + pad | IMU (+traj) | SVG/medians | images+masks | Tomoe XML / `.model` | SVG | JSON strokes |
| Licence | Academic agreement | **Unspecified** | CC BY-NC 4.0 | Unclear | MIT (stated) | LGPL-2.1 | CC BY-SA 3.0 | Consent TBD |
| Commercial use | Charged / contact | Unknown | No (NC) | Unknown | Likely yes | Yes (LGPL) | Yes w/ BY-SA | Only if consented |
| Redistribution of samples | No | Unknown | NC limits | Unknown | Yes (MIT) | LGPL | BY-SA | Consent |
| Model training | Academic only | Unknown | NC | Unknown | Allowed | Allowed | Allowed w/ BY-SA risk | If consented |
| Distribute trained model | **Unresolved / no** for OSS shipping | Unknown | No | Unknown | Likely | Yes under LGPL | BY-SA risk | If consented |
| Attribution | Required (cite Liu et al.) | Unknown | Cite papers | Cite card | Cite paper | LGPL notices | BY | Per consent |

---

## Bottom line

- The HF “CASIA online” dataset is **not** verified stroke data; official `.pot` is. Either way, CASIA is **research-only** for licensing and is a poor primary fit for traditional/historical Japanese shipping models.
- marinaMoji already has a licensed stack (Zinnia + Tegaki JA/TW), real trackpad samples, a solid eval harness, and a neural hybrid that targets Zinnia’s stroke-order weakness.
- **Smallest useful training experiment:** retrain/adapt on **Tegaki Tomoe + consented own ink**, evaluate on **held-out `samples-test`**, leave CASIA in a private sandbox if needed for research curiosity — never in release artifacts.
