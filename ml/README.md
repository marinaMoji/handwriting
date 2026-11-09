# ml/: order-free neural recognizer prototype (issue #16)

Offline prototype, not part of the app build. Design and plan: [docs/neural-scope.md](../docs/neural-scope.md).

```bash
python3 -m venv .venv-ml && .venv-ml/bin/pip install torch numpy pillow    # gitignored
bash scripts/fetch_models.sh                                               # the training XMLs live in models_dl/
.venv-ml/bin/python -m ml.contact_sheet ml/out/sheet.png                   # real samples vs augmented renders
.venv-ml/bin/python -m ml.train base1 --steps 20000                        # ~55 min on an M1 Pro (MPS)
.venv-ml/bin/python -m ml.eval_real ml/runs/base1 --dump /tmp/neural.json  # real samples in samples/
./build/handwriting_eval --samples samples --modes ja,tw --dump /tmp/zinnia.json \
    --variants "resample:4+smooth:3@merge+split+count" --tol-penalty 0.1
.venv-ml/bin/python -m ml.hybrid /tmp/zinnia.json /tmp/neural.json         # fusion, cross-validated by character half
```

Training data: Tegaki templates (LGPL-2.1), augmented on the fly. `samples/` is only used for evaluation, never for training. Weights (`ml/runs/`) are gitignored.
