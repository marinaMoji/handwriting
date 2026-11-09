"""Fixtures for tests/ink_net_test.cpp: renderer outputs and a tiny random InkNet's logits.

    python -m ml.make_fixtures

Writes tests/data/ink_fixture.json and tests/data/tiny.inknet. The tiny net has random weights
and random BatchNorm statistics (so BN folding is exercised), so the test needs no trained
weights, and the real trained net is checked separately by ml.eval_real / handwriting_eval.
"""
import base64
import json

import numpy as np
import torch

from .export import write_inknet
from .model import InkNet
from .render import render

CASES = [
    {"width": 1.8, "strokes": [[[40, 150], [260, 150]], [[150, 40], [150, 260]]]},
    {"width": 2.2, "strokes": [[[60, 60], [90, 40], [150, 30], [220, 60], [250, 130], [200, 230], [80, 260]]]},
    {"width": 1.4, "strokes": [[[100, 100]], [[200, 100], [200.2, 100.1]], [[60, 220], [240, 230]]]},
    {"width": 1.8, "strokes": [[[20, 20], [280, 280]], [[280, 20], [20, 280]], [[150, 10], [150, 290], [140, 100]]]},
]
WIDTHS = (4, 8, 8, 8)


def main():
    torch.manual_seed(7)
    net = InkNet(20, widths=WIDTHS)
    for m in net.modules():
        if isinstance(m, torch.nn.BatchNorm2d):
            m.running_mean.copy_(torch.randn_like(m.running_mean) * 0.2)
            m.running_var.copy_(torch.rand_like(m.running_var) + 0.5)
            m.weight.data.copy_(torch.rand_like(m.weight) + 0.5)
            m.bias.data.copy_(torch.randn_like(m.bias) * 0.2)
    net.eval()
    classes = [f"c{i}" for i in range(19)] + ["漢"]
    write_inknet(net, classes, "tests/data/tiny.inknet", WIDTHS)
    out = []
    for case in CASES:
        strokes = [np.array(s, dtype=np.float64) for s in case["strokes"]]
        img = render(strokes, width=case["width"])
        with torch.no_grad():
            logits = net(torch.from_numpy(img[None])).numpy()[0]
        out.append({**case, "image": base64.b64encode(img.tobytes()).decode(), "logits": logits.tolist()})
    json.dump({"cases": out}, open("tests/data/ink_fixture.json", "w"))
    print("wrote fixtures:", len(out), "cases")


if __name__ == "__main__":
    main()
