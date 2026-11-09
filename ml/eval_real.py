"""Evaluate a trained InkNet on real samples (same pre-pipeline as the app: specks, fit, render).

usage: python -m ml.eval_real RUN_DIR [LABEL=DIR ...] [--dump scores.json] [--topk 30]
Default: all of samples/. Prints top-1/5/10 per label. --dump writes per-file top-k
(character, log-probability) lists for the C++ hybrid evaluation.
"""
import glob
import json
import os
import sys

import numpy as np
import torch

from .model import InkNet
from .render import prepare_sample
from .tegaki import ROOT


def load_run(run_dir, device="cpu"):
    meta = json.load(open(os.path.join(run_dir, "classes.json"), encoding="utf-8"))
    model = InkNet(len(meta["classes"]))
    model.load_state_dict(torch.load(os.path.join(run_dir, "model.pt"), map_location="cpu"))
    return model.to(device).eval(), meta["classes"]


@torch.no_grad()
def predict(model, classes, stroke_lists, device="cpu", topk=30, batch=128):
    out = []
    for i in range(0, len(stroke_lists), batch):
        x = torch.from_numpy(np.stack([prepare_sample(s) for s in stroke_lists[i:i + batch]])).to(device)
        lp = torch.log_softmax(model(x), dim=1)
        v, ix = lp.topk(topk, dim=1)
        for vals, idx in zip(v.cpu().numpy(), ix.cpu().numpy()):
            out.append([(classes[j], float(a)) for j, a in zip(idx, vals)])
    return out


def load_samples(d):
    items = []
    for f in sorted(glob.glob(os.path.join(d, "*.json"))):
        j = json.load(open(f, encoding="utf-8"))
        items.append((os.path.basename(f), j["character"], j["strokes"]))
    return items


def metrics(preds, truths):
    n = len(truths)
    r = {1: 0, 5: 0, 10: 0}
    for p, t in zip(preds, truths):
        chars = [c for c, _ in p]
        rank = chars.index(t) + 1 if t in chars else 999
        for k in r:
            r[k] += rank <= k
    return {k: 100.0 * v / max(n, 1) for k, v in r.items()}, n


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    run = args[0]
    groups = [a.split("=", 1) for a in args[1:] if "=" in a] or [("all", os.path.join(ROOT, "samples"))]
    topk = int(sys.argv[sys.argv.index("--topk") + 1]) if "--topk" in sys.argv else 30
    dump = sys.argv[sys.argv.index("--dump") + 1] if "--dump" in sys.argv else None
    model, classes = load_run(run)
    allscores = {}
    for label, d in groups:
        items = load_samples(d)
        preds = predict(model, classes, [s for _, _, s in items], topk=topk)
        m, n = metrics(preds, [c for _, c, _ in items])
        print(f"{label:<12} n={n:<4} top1 {m[1]:5.1f}  top5 {m[5]:5.1f}  top10 {m[10]:5.1f}")
        for (name, _, _), p in zip(items, preds):
            allscores[name] = p
    if dump:
        json.dump(allscores, open(dump, "w", encoding="utf-8"), ensure_ascii=False)
        print("wrote", dump)


if __name__ == "__main__":
    main()
