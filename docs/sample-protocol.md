# Sample collection protocol

Goal: real handwriting samples to evaluate and tune the recognizer on *your* writing rather than synthetic ink. Issue #9.

## How

1. Run the app, hold the hold-key, write one character.
2. Type the character you meant in the **Expected** box (2 characters max; non-BMP like 𠮷 works).
3. Press **Return** or **Save sample**. The canvas clears and refocuses, ready for the next one.
4. Check progress any time:

```bash
python3 scripts/sample_coverage.py            # one pass: >= 1 sample per listed character
python3 scripts/sample_coverage.py --quick 2  # second pass: >= 2 samples per character
```

The character lists are in [sample-chars.json](sample-chars.json) (213 entries in 7 categories: simple, common Japanese, traditional forms, variant pairs, similar shapes, complex, rare).

## Sittings

| Sitting | Do | Time |
|---|---|---|
| 1 | Simple, common Japanese, similar shapes (~100) | ~15 min |
| 2 | Traditional forms, variant pairs (~70): the most valuable set, it decides Both-mode fusion | ~15 min |
| 3 | Complex, rare (~40), then a second quick pass over anything you like | ~15 min |

## Practice mode (prompted collection)

Faster than typing the expected character for every sample, and it keeps writers' data separable.

1. Press **Writer…** once: a pseudonym (letters, digits, `-`, `_`; not a name), the writing hand, and the device. It is stored in every sample (`writer`, `handedness`, `device`) and remembered between runs.
2. Press **Practice**, pick a list, and write the character shown. **Return** saves and moves on, **Backspace** clears the ink to try again, **→** skips (the character comes back at the end), **←** deletes the sample saved last. For a left-handed writer on the trackpad, hold **F12** to write and use the right hand for these keys.
3. Recognition is hidden by default so it cannot change how you write (tick *Show recognition* to see it). Quit any time: the next session resumes with the characters this writer has not written yet.

| List | Writes to | Purpose |
|---|---|---|
| Held-out core (150 characters) | `samples-test/` (`set: test`) | The same list for every writer, so writers can be compared. Never tuned on. Contains none of the development characters. |
| More development characters (300) | `samples/` (`set: dev`) | Broader coverage for tuning and training experiments. |
| The seven lists of `sample-chars.json` | `samples/` | The original development lists. |

The two generated lists come from `scripts/make_practice_sets.py` (fixed seed, stratified by stroke count, drawn from jōyō kanji, traditional / jinmeiyō forms and other common ideographs, with the development characters excluded). Their order is shuffled per writer. The prompt is a printed glyph, so this is copying rather than writing from memory, and a font's shapes can differ from what a writer would produce.

Each sample also records `set`, `session` (one per app run), and `prompted` (true when the app chose the character). Evaluate one writer with `handwriting_eval --samples samples-test --writer w1`, and check progress with `scripts/sample_coverage.py --writer w1`. Older samples have no writer and still load.

## Rules that keep the data useful

- Write naturally on the trackpad; do not adapt to the recognizer. Wrong results are the point.
- Write in the stroke order you normally use. Do not "correct" it to match a textbook, and do not discard sloppy samples: stroke-order and stroke-count variation is exactly what the recognizer has to handle.
- Do not save a sample with the wrong Expected character; clear the canvas and redo it.
- Variant pairs: write both members of each pair (e.g. 國 and 国).
- One character per sample.

## Sample sets

| Directory | Purpose | Rule |
|---|---|---|
| `samples/` | Development set (first 206 samples): used to build and tune | Free to look at and tune on |
| `samples-test/` | Held-out test set, written later (#18) | Never tuned on; reported once before the go/no-go decision |
| `samples-textbook/` | ~50 characters in standard stroke order (#19) | Isolates stroke-order effects; optional |

Write into a set with `HANDWRITING_SAMPLES_DIR=samples-test ./build/handwriting_app`. All three directories are gitignored (the repo is public); back them up yourself.

## Checking samples

```bash
python3 scripts/check_stroke_order.py [dir]   # flags stroke order/count differing from KanjiVG
./build/handwriting_eval --samples samples --modes ja,tw,both --variants raw,none,resample:4+smooth:3 --misses
```

`check_stroke_order.py` is a heuristic: flagged samples are real data, keep them. On the first 206 samples, flagged ones were recognized far worse (ja top-1 22-37% vs 67% for unflagged), which is why stroke-order/count tolerance (#13) is the priority.
