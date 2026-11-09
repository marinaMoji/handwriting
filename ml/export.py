"""Export a trained InkNet to the flat binary read by src/InkNet.cpp (BatchNorm folded away).

    python -m ml.export ml/runs/base2 out.inknet

Layout (little endian):
    "INKN" u32 version(1) u32 in_ch u32 n_widths u32 widths[n] u32 n_classes then per class u8 length + UTF-8 bytes (a few classes are multi-character, e.g. "(^^)")
    then the convolutions in network order, each as float32 weights [cout][cin][k][k] + bias [cout]:
        stem; for every stage: block1.c1, block1.c2, block1.skip (always: stride 2), block2.c1, block2.c2
    then the classifier: float32 weights [n_classes][last_width] + bias [n_classes].
Inference is: stem+ReLU, blocks (ReLU(conv-bn-ReLU-conv-bn + skip)), global average pool, linear.
"""
import json
import struct
import sys

import numpy as np
import torch

from .model import InkNet


def _fold(conv, bn):
    w = conv.weight.detach().double()
    scale = bn.weight.detach().double() / torch.sqrt(bn.running_var.detach().double() + bn.eps)
    bias = bn.bias.detach().double() - bn.running_mean.detach().double() * scale
    return (w * scale[:, None, None, None]).float().numpy(), bias.float().numpy()


def write_inknet(model, classes, path, widths):
    model.eval()
    out = bytearray(b"INKN")
    out += struct.pack("<III", 1, model.stem[0].in_channels, len(widths))
    out += struct.pack(f"<{len(widths)}I", *widths)
    out += struct.pack("<I", len(classes))
    for c in classes:
        b = c.encode("utf-8")
        out += struct.pack("<B", len(b)) + b

    def emit(w, b):
        out.extend(np.ascontiguousarray(w, dtype="<f4").tobytes())
        out.extend(np.ascontiguousarray(b, dtype="<f4").tobytes())

    emit(*_fold(model.stem[0], model.stem[1]))
    for blk in model.body:
        emit(*_fold(blk.c1, blk.b1))
        emit(*_fold(blk.c2, blk.b2))
        if blk.skip is not None:
            emit(*_fold(blk.skip[0], blk.skip[1]))
    emit(model.fc.weight.detach().float().numpy(), model.fc.bias.detach().float().numpy())
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def main():
    run, path = sys.argv[1], sys.argv[2]
    classes = json.load(open(f"{run}/classes.json", encoding="utf-8"))["classes"]
    codepoints = classes
    model = InkNet(len(classes))
    model.load_state_dict(torch.load(f"{run}/model.pt", map_location="cpu"))
    n = write_inknet(model, classes, path, (32, 64, 128, 256))
    print(f"wrote {path}: {n / 1e6:.1f} MB, {len(classes)} classes")


if __name__ == "__main__":
    main()
