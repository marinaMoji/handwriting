"""Write a PNG comparing real samples with augmented renders of the same characters.

usage: python -m ml.contact_sheet OUT.png [samples_dir] [chars]
Left block: your real samples (9-channel render shown as ink). Right block: augmented renders.
"""
import glob
import json
import os
import sys

import numpy as np
from PIL import Image

from .augment import augment
from .render import fit_to_canvas, prepare_sample, render
from .tegaki import load_templates, ROOT


def main():
    out = sys.argv[1]
    sdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "samples")
    chars = sys.argv[3] if len(sys.argv) > 3 else "大日月田語學體龍馬國"
    templates = load_templates()
    rng = np.random.default_rng(3)
    real = {}
    for f in sorted(glob.glob(os.path.join(sdir, "*.json"))):
        d = json.load(open(f, encoding="utf-8"))
        real.setdefault(d["character"], []).append(d["strokes"])
    rows = []
    for ch in chars:
        cells = []
        for st in (real.get(ch) or [])[:3]:
            cells.append(prepare_sample(st)[0])
        while len(cells) < 3:
            cells.append(np.zeros((64, 64), np.uint8))
        cells.append(np.full((64, 4), 128, np.uint8).repeat(1, 1)[:, :4].reshape(64, 4)) if False else None
        sep = np.full((64, 6), 90, np.uint8)
        aug = []
        if ch in templates:
            for _ in range(6):
                v = templates[ch][int(rng.integers(len(templates[ch])))]
                aug.append(render(fit_to_canvas(augment(v, rng)), width=float(rng.uniform(1.4, 2.4)))[0])
        while len(aug) < 6:
            aug.append(np.zeros((64, 64), np.uint8))
        rows.append(np.concatenate(cells + [sep] + aug, axis=1))
    sheet = np.concatenate(rows, axis=0)
    Image.fromarray(255 - sheet).resize((sheet.shape[1] * 2, sheet.shape[0] * 2), Image.NEAREST).save(out)
    print("wrote", out, sheet.shape)


if __name__ == "__main__":
    main()
