# Arphic Public License and trained handwriting models

**Status:** legal research note for marinaMoji — not legal advice.  
**Date:** 2026-11-13  
**Licence text:** [ARPHICPL.TXT](https://raw.githubusercontent.com/chanind/hanzi-writer-data/master/ARPHICPL.TXT) (Arphic Technology Co., Ltd., 1999); SPDX: `Arphic-1999`.

The policy adopted from this note, with provenance, modification notices, reproduction steps and the distribution checklist, is in [`licensing/handwriting/`](licensing/handwriting/README.md); the legal review is tracked in [issue #21](https://github.com/marinaMoji/handwriting/issues/21).

This note asks what we must do if we distribute a Zinnia (or neural) model trained in part on Hanzi Writer / Make Me a Hanzi **graphics** (stroke medians derived from Arphic fonts).

---

## What the licence covers

APL §0 defines **“Font”** as four named TrueType fonts **and**

> the derivatives of those fonts created through any modification including modifying glyph, reordering glyph, **converting format**, changing font name, or adding/deleting some characters in/from glyph table.

Make Me a Hanzi and Hanzi Writer treat `graphics.txt` / per-character JSON (strokes + medians) as **APL-covered derivatives** of those fonts. Redistributing that stroke data requires keeping `ARPHICPL.TXT` and, for modifications, the §2 conditions.

---

## Obligations when redistributing Font / stroke data

| Obligation | Source | Practical meaning |
|------------|--------|-------------------|
| Keep `ARPHICPL.TXT` unaltered with every copy | §1 | Ship the licence file next to any redistributed medians/JSON/train dumps derived from HZW |
| Modification notice (how/when changed) | §2(a) | Document conversion (coords, augmentation) in a prominent NOTICE |
| Make modifications Freely Available under APL | §2(b) | Publish the modified stroke corpus (or offer a download place); recipients get the same freedoms |
| No extra restrictions on recipients | §5 | Cannot add a proprietary EULA that forbids further APL use of the Font derivative |
| Conflict → may not distribute the Font | §6 | If another licence on the *same* work blocks royalty-free APL redistribution, you cannot ship that Font derivative |
| No warranty | §7–8 | Disclaim as usual |

**Aggregation carve-out (§2 last paragraph):** packaging an independent work *with* the Font on the same medium does **not** automatically put that other work under APL — if it is “not derived from the Font” and can stand alone.

---

## Does a trained `.model` count as a “Font” derivative?

APL never mentions machine-learning weights. Two readings:

### Conservative (safer for an open-source IME)

Stroke medians are Font derivatives. A recognizer trained so that its parameters encode those trajectories is a further transformation of that data. Treat the **experimental model** like a modified Font for compliance purposes:

- Ship `ARPHICPL.TXT` + a NOTICE describing the HZW→Zinnia pipeline and date.
- Make the **training inputs derived from HZW** Freely Available under APL (the `.s` lines or a regenerable script + pointer to upstream HZW), not only the binary.
- Do **not** impose terms that conflict with APL on the HZW-derived portion.
- Keep Tegaki/LGPL material clearly separable in notices (Tegaki templates remain LGPL-2.1; HZW extras are APL).

Under this reading, **shipping is allowed** if you comply with §1–2 and §5 — APL is copyleft for the Font, not a ban on commercial use. “Freely Available” means freedom to copy/modify under APL, not “zero price.”

### Narrow (optimistic)

A Zinnia SVM / CNN weight file is an independent statistical artifact, not a glyph table or format conversion of the Font. APL would then apply to redistributed **stroke corpora**, not to weights, while the aggregation clause lets you ship weights next to (or without) the Font data.

This reading is **unresolved** in case law and in Arphic’s text. Relying on it alone for a public marinaMoji release is risky.

---

## Interaction with Tegaki (LGPL-2.1) and marinaMoji

- Production Tegaki JA/TW models: **LGPL-2.1**, Tomoe-derived — no APL.
- Full-overlap experiments: Tegaki lines **plus** HZW lines in one trainer run → the **binary may mix** both lineages.
- Mixing does **not** relicense Tegaki under APL or vice versa by magic, but a **single fused `.model`** is hard to split. Practical options:

  1. **Don’t ship** fused experimental models until counsel reviews.  
  2. Ship fused models only with **both** LGPL notices **and** full APL compliance for the HZW-derived contribution (NOTICE + Freely Available training extras + `ARPHICPL.TXT`).  
  3. Prefer architectures that keep an **APL-free** path (Tegaki-only or consented marinaMoji ink) for the default download.  
  4. **Separate recognizers** (Tegaki production ∥ HZW-only, list fusion) keep lineages split for packaging, but on the 214-sample dev set they underperform fused `full-clean` training (~68.7 vs 71.0 top-1 best fuse — see [`hanzi-writer-experiment.md`](hanzi-writer-experiment.md) §4d). Use separate only if counsel requires it; do not expect an accuracy win.

Neural hybrid weights trained **only** on Tegaki templates (current `InkNet`) stay on the LGPL/Tegaki side. Retraining InkNet on HZW medians would raise the same APL questions as Zinnia.

---

## Recommended policy for marinaMoji (until counsel says otherwise)

1. **Production downloads:** keep Tegaki-only models; do not replace with HZW-augmented weights.  
2. **Research artifacts:** may live under `experiments/` with `ARPHICPL.TXT`, regeneration scripts, and clear “not for release” labelling.  
3. **If releasing an HZW-augmented model later:**  
   - comply with APL §1–2 for all HZW-derived training material;  
   - publish NOTICE + how to regenerate;  
   - dual-notice LGPL (Tegaki) + APL (HWZ);  
   - avoid adding conflicting licence terms;  
   - get a short legal review on whether weights themselves must be APL.  
4. **Do not** treat “HF / GitHub hosts the JSON” as permission to ignore APL when shipping derivatives.

---

## Short answers

| Question | Answer |
|----------|--------|
| May we *train* on HZW medians privately? | Yes for local research; upstream allows use under APL. |
| May we *redistribute* converted medians / train dumps? | Yes, if `ARPHICPL.TXT` + §2 notices/Freely Available. |
| May we *redistribute* a Zinnia model trained with those medians? | **Unresolved**; conservative path is “yes only with full APL compliance as if the model were a Font derivative,” or don’t ship until reviewed. |
| Must recipients get source of the Font derivative? | For modifications, §2(b) requires making those modifications Freely Available under APL — publish regenerable HZW-derived train data, not necessarily Tegaki XML (already LGPL elsewhere). |
| Commercial IME use? | APL is not NC; commercial distribution of compliant Font derivatives is contemplated. Conflict with other obligations (§6) is the hard stop. |
