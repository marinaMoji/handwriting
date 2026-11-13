# Licensing of handwriting models

This directory covers one question: under what terms marinaMoji may distribute handwriting-recognition models, in particular a model trained with Hanzi Writer stroke data.

None of this is legal advice. The legal review is tracked in [issue #21](https://github.com/marinaMoji/handwriting/issues/21), and **no Hanzi Writer-trained model may be published until that issue authorizes it.**

| File | Content |
|---|---|
| [PROVENANCE.md](PROVENANCE.md) | Where each training source comes from, with pinned versions and hashes |
| [MODIFICATIONS.md](MODIFICATIONS.md) | Exactly how Hanzi Writer medians become Zinnia training examples and weights |
| [REPRODUCING.md](REPRODUCING.md) | Commands to regenerate the corpora, the model and the staged package |
| [DISTRIBUTION-CHECKLIST.md](DISTRIBUTION-CHECKLIST.md) | What must be true before anything is published |
| [sources.json](sources.json) | Machine-readable pins and expected hashes, used by the build script and the tests |
| [licenses/](licenses/) | `ARPHICPL.TXT` and `LGPL-2.1.txt`, byte-for-byte as received from upstream |

The analysis of the licence text is in [../../arphic-pl-model-distribution.md](../../arphic-pl-model-distribution.md) and is not repeated here.

## What is distributed today

| Model | Training data | Licence | Status |
|---|---|---|---|
| `handwriting-ja.model` | Tegaki Japanese 0.3 | LGPL-2.1 | Production, downloaded by `scripts/fetch_models.sh` |
| `handwriting-zh_TW.model` | Tegaki Traditional Chinese 0.3 | LGPL-2.1 | Production, same |
| InkNet weights | Tegaki templates with synthetic augmentation | LGPL-2.1 lineage | Optional hybrid mode |
| `handwriting-ja-hzw-experimental.model` | Tegaki Japanese 0.3 plus Hanzi Writer medians | See below | **Staged only, not distributed** |

## Confirmed licence requirements

These follow directly from the licence texts and apply whenever the corresponding material is redistributed.

For the Hanzi Writer stroke data and any modified form of it (Arphic Public License):

- Keep `ARPHICPL.TXT` unaltered with every copy (§1).
- Put a prominent notice in each modified file stating how and when it was changed (§2(a)).
- Make the modifications Freely Available as a whole under the same licence, for example by offering them from a designated place (§2(b)).
- Add no further restrictions on recipients (§5).
- If the licence cannot be satisfied together with another obligation, do not distribute (§6).

For the Tegaki templates and models (GNU LGPL 2.1):

- Keep the licence text and copyright notices with every copy.
- Mark modified files as changed, with the date (§2(a), §2(b)).
- Provide the corresponding source, or an offer of it, with any binary form (§4).

## Conservative project policy

These go beyond what the texts clearly require. marinaMoji adopts them so that distribution does not depend on an untested argument. They are assumptions for compliance purposes, not a legal interpretation.

1. Hanzi Writer stroke medians are treated as APL-covered derivatives of Arphic fonts.
2. Converted and augmented training data derived from them remain APL-covered.
3. Zinnia weights trained on that data are treated as APL-covered derivatives, although their legal classification is unresolved. The model is therefore shipped with `ARPHICPL.TXT`, a modification notice, and the training files and scripts needed to regenerate it.
4. Tegaki-derived training material keeps its LGPL-2.1 obligations, and the Tegaki-derived training file is shipped as the corresponding source for its contribution to the model.
5. A single model trained on both sources is not distributed until it is confirmed that both licences can be satisfied at once (issue #21, question 2).
6. The rest of marinaMoji is not treated as subject to APL merely because it distributes or loads such a model, provided the components are independent. The application has no build-time or run-time dependency on Hanzi Writer data, and the model is a separate, optional file.
7. Publishing a regeneration script is not assumed to be enough. The converted Hanzi Writer-derived corpus itself is included in the package, separate from the Tegaki-derived one.

## Distribution policy

- Production downloads stay Tegaki-only. `scripts/fetch_models.sh`, CI and the application defaults do not reference any Hanzi Writer-derived file.
- The Hanzi Writer-enhanced model exists only as a staged package under `experiments/hzw-package/` (ignored by git), built by `scripts/build_hzw_model_package.py`. Its manifest carries `"release_authorized": false`.
- The staged model has a distinct file name and is never placed in `models/`.
- InkNet weights are unchanged and are not part of the package.
- Publication, in any channel, requires every item of [DISTRIBUTION-CHECKLIST.md](DISTRIBUTION-CHECKLIST.md), including the closing of issue #21.

## Open questions

They are listed in issue #21. The one that blocks release is whether a fused Tegaki and Hanzi Writer model can satisfy LGPL-2.1 and APL simultaneously. If the answer is no, the fallback is two separate recognizers, one per licence, with candidate fusion at run time; `--no-tegaki` in `scripts/hanzi_writer_to_zinnia.py` already produces the APL-only corpus for that.
