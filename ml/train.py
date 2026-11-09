"""Train InkNet on augmented Tegaki templates.

usage: python -m ml.train RUN_NAME [--steps N] [--batch B] [--workers W] [--lr LR] [--bench]
Outputs ml/runs/RUN_NAME/{model.pt,classes.json,log.txt}. Evaluates on the real samples in
samples/ (read-only, never trained on) every --eval-every steps.
"""
import json
import os
import sys
import time

import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from .dataset import TemplateStream
from .eval_real import load_samples, metrics, predict
from .model import InkNet
from .tegaki import ROOT, load_templates


def arg(name, default, cast=float):
    return cast(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def main():
    name = sys.argv[1]
    steps = arg("--steps", 20000, int)
    batch = arg("--batch", 256, int)
    workers = arg("--workers", 8, int)
    lr = arg("--lr", 2e-3)
    eval_every = arg("--eval-every", 2000, int)
    bench = "--bench" in sys.argv
    out = os.path.join(ROOT, "ml", "runs", name)
    os.makedirs(out, exist_ok=True)
    log = open(os.path.join(out, "log.txt"), "a")

    def say(msg):
        print(msg, flush=True)
        log.write(msg + "\n")
        log.flush()

    templates = load_templates()
    classes = sorted(templates)
    json.dump({"classes": classes}, open(os.path.join(out, "classes.json"), "w", encoding="utf-8"), ensure_ascii=False)
    say(f"{len(classes)} classes, {sum(len(v) for v in templates.values())} templates")

    device = "mps" if torch.backends.mps.is_available() else "cpu"
    model = InkNet(len(classes)).to(device)
    say(f"device {device}, params {sum(p.numel() for p in model.parameters())/1e6:.2f}M")
    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=0.05)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=lr, total_steps=steps, pct_start=0.1)
    loader = DataLoader(TemplateStream(templates, classes, seed=int(time.time()) % 100000),
                        batch_size=batch, num_workers=workers, persistent_workers=True, prefetch_factor=4)
    real = load_samples(os.path.join(ROOT, "samples"))

    model.train()
    t0 = time.time()
    run_loss = run_acc = 0.0
    for step, (x, y) in enumerate(loader, 1):
        x, y = x.to(device), y.to(device)
        logits = model(x)
        loss = F.cross_entropy(logits, y, label_smoothing=0.1)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        opt.step()
        sched.step()
        run_loss += loss.item()
        run_acc += (logits.argmax(1) == y).float().mean().item()
        if step % 100 == 0:
            el = time.time() - t0
            say(f"step {step}/{steps} loss {run_loss/100:.3f} acc {100*run_acc/100:.1f}% "
                f"{step*batch/el:.0f} img/s  lr {sched.get_last_lr()[0]:.5f}")
            run_loss = run_acc = 0.0
        if bench and step >= 200:
            return
        if step % eval_every == 0 or step == steps:
            model.eval()
            preds = predict(model, classes, [s for _, _, s in real], device=device)
            m, n = metrics(preds, [c for _, c, _ in real])
            say(f"  REAL n={n} top1 {m[1]:.1f} top5 {m[5]:.1f} top10 {m[10]:.1f}")
            torch.save(model.state_dict(), os.path.join(out, "model.pt"))
            model.train()
        if step >= steps:
            break
    say(f"done in {(time.time()-t0)/60:.1f} min")


if __name__ == "__main__":
    main()
