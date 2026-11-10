# ml/: order-free neural recognizer (issue #16)

Training and evaluation live here; the app/CLI runtime is C++ ([src/InkNet.cpp](../src/InkNet.cpp)). Design and results: [docs/neural-scope.md](../docs/neural-scope.md).

```bash
python3 -m venv .venv-ml && .venv-ml/bin/pip install torch numpy pillow    # gitignored
bash scripts/fetch_models.sh                                               # the training XMLs live in models_dl/
.venv-ml/bin/python -m ml.contact_sheet ml/out/sheet.png                   # real samples vs augmented renders (AUG=2 for level 2)
.venv-ml/bin/python -m ml.train base2 --steps 20000 [--aug 2]              # ~85 min on an M1 Pro (MPS)
bash scripts/export_weights.sh base2                                       # -> ml/runs/base2/model.inknet (what C++ loads)
./build/handwriting_eval --samples samples --modes both --neural ml/runs/base2/model.inknet \
    --neural-mode hybrid --neural-weight 4 --variants "resample:4+smooth:3@merge+split+count"
```

## Re-tuning when there are more samples
1. Add samples to `samples/` (dev) -- never to `samples-test/` (held-out, #18).
2. Retrain (optionally with `--aug 2`), export, evaluate as above.
3. Pick `--neural-weight` by sweeping on the new samples (`ml.hybrid` does a cross-validated sweep from `--dump` files of both systems; `ml.eval_real RUN --dump` and `handwriting_eval --dump` produce them).
Samples are only ever used for evaluation and tuning, never as training data, unless that is changed deliberately.

## Files
- `render.py`: the order-free 9-channel renderer (spec shared with C++); `augment.py`: distortion model (level 1, level 2); `dataset.py`, `model.py`, `train.py`.
- `export.py`: trained net -> flat weights with BatchNorm folded; `make_fixtures.py`: regenerates `tests/data/` (renderer and tiny-net fixtures checked by `ink_net_test`). **If you change `render.py`, `model.py` or `export.py`, rerun `python -m ml.make_fixtures` and retrain.**
- `eval_real.py`, `hybrid.py`: Python-side evaluation and fusion sweeps.

Training data: Tegaki templates (LGPL-2.1), augmented on the fly. Weights (`ml/runs/`) are gitignored.
