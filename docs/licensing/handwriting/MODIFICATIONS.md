# Modifications: from Hanzi Writer medians to a Zinnia model

Date of modification: **2026-11-13**. Modified by: the marinaMoji project.
Code: `scripts/hanzi_writer_to_zinnia.py` (conversion) and `scripts/build_hzw_model_package.py` (notices, training, packaging).

This is the full statement of "how and when" the Arphic-derived data was changed. A shorter form is inserted at the top of each modified training file, and a notice accompanies the binary model.

## 1. Input

One file per character, `data/<character>.json`, from Hanzi Writer data at commit `68d10a4`. Each file has two keys:

- `strokes`: SVG outline paths of each stroke. **Not used.**
- `medians`: for each stroke, a short polyline along its centre, in writing order. **Used.**

Coordinates are in a 1024 by 1024 box whose y axis points up: the top of the glyph box is y = 900 and the bottom is y = -124.

## 2. Selection

Only characters that also exist in tegaki-zinnia-japanese 0.3 are kept: 5,029 characters. The selection is by Unicode code point and does not look at any evaluation sample. One example is produced per character. The Tegaki file contributes only the list of characters to this step.

## 3. Coordinate conversion

Each point (x, y) becomes

```text
x' = round(x / 1024 * 1000)
y' = round((900 - y) / 1024 * 1000)
```

clamped to 0..1000. The result is the Tomoe/Zinnia convention: a 1000 by 1000 box with y pointing down.

Then, within each stroke, a point identical to the previous one is dropped. A stroke left with a single point gets a second point one unit to the right, so that it remains a stroke. Stroke order and stroke count are never changed.

## 4. No synthetic distortion

The distributed configuration ("full-clean") applies no augmentation. The converter also has an augmentation mode (wobble, jitter, scaling, rotation), used in other experiments; it is not used for this corpus or this model.

## 5. Serialization

Each character is written as one line in Zinnia's S-expression training format:

```text
(character (value 一)(width 1000)(height 1000)(strokes ((118 495)(188 516)(407 486)(808 455)(898 487))))
```

The file starts with comment lines (prefix `;`) carrying the modification notice. `zinnia_learn` skips them; we checked that a model trained with and without these lines is byte-identical.

Result: `hzw-derived.ja.s`, 5,029 lines, Arphic Public License.

## 6. Tegaki portion

`handwriting-ja.xml` is converted to the same format without any change to the points: 6,646 templates for 6,421 characters. It also gets a notice header (LGPL-2.1 §2 asks for one). Result: `tegaki-derived.ja.s`, LGPL-2.1.

## 7. Training

```text
cat tegaki-derived.ja.s hzw-derived.ja.s > combined.ja.s     (11,675 examples)
zinnia_learn combined.ja.s handwriting-ja-hzw-experimental.model
```

Zinnia turns each example into a sparse feature vector computed from its strokes and trains one linear SVM per character against all others, with default parameters. The model file stores, for each of the 6,421 characters, a bias and a list of (feature index, weight) pairs. It does not store the training strokes as such, and glyph outlines never enter the pipeline.

Characters outside the 5,029-character overlap are trained on their Tegaki template only, but their weights still depend on the Hanzi Writer examples of other characters, which serve as negative examples. The two lineages therefore cannot be separated inside the binary.

## 8. Notices

| File | Where the notice is |
|---|---|
| `hzw-derived.ja.s` | comment header in the file |
| `tegaki-derived.ja.s` | comment header in the file |
| `handwriting-ja-hzw-experimental.model` | `…model.NOTICE.txt` beside it, naming the model by sha256, and the package `NOTICE.txt` |

The Zinnia binary format has no comment field and its loader rejects a file whose length differs from the recorded one, so a notice cannot be inserted without changing the format. Whether a notice next to the file satisfies APL §2(a) ("in each modified file") for a binary is question 4 of issue #21.

## 9. What is not modified

`ARPHICPL.TXT` and the LGPL text are copied byte-for-byte. The original Hanzi Writer JSON files and the Tegaki XML are not redistributed by marinaMoji; they are referenced by commit and hash in [PROVENANCE.md](PROVENANCE.md).
