"""Order-free rendering of ink: fit like the C++ recognizer, then draw ink + 8 direction channels.

Channel 0 is the ink; channels 1..8 hold the same ink split by stroke direction (soft-assigned
to the two nearest of 8 direction bins). Stroke order and count are not represented.
"""
import math

import numpy as np
from PIL import Image, ImageDraw

CANVAS = 300  # same canvas the C++ pipeline fits to
NUM_CHANNELS = 9


def drop_specks(strokes, max_len=8.0, max_points=2):
    """Same rule as stroke_preprocess::dropSpecks; never returns an empty list."""
    keep = []
    for s in strokes:
        if len(s) <= max_points and (len(s) < 2 or float(np.hypot(*(s[1:] - s[:-1]).T).sum()) < max_len):
            continue
        keep.append(s)
    return keep or list(strokes)


def fit_to_canvas(strokes, canvas=CANVAS, margin=24, max_scale=4.0):
    """Port of stroke_mapping::fitToCanvas (float instead of int coordinates)."""
    pts = np.concatenate([s for s in strokes if len(s)], axis=0)
    mn, mx = pts.min(axis=0), pts.max(axis=0)
    bw, bh = max(1.0, mx[0] - mn[0]), max(1.0, mx[1] - mn[1])
    avail = max(1, canvas - 2 * margin)
    scale = min(avail / bw, avail / bh, max_scale)
    c = (mn + mx) / 2.0
    return [np.clip((s - c) * scale + canvas / 2.0, 0, canvas - 1) for s in strokes]


def render(strokes, size=64, width=1.8, ss=2):
    """strokes: list of (N,2) arrays already in the 300x300 canvas. Returns uint8 (9, size, size)."""
    big = size * ss
    k = big / CANVAS
    w = max(1, int(round(width * ss)))
    imgs = [Image.new("L", (big, big), 0) for _ in range(NUM_CHANNELS)]
    draws = [ImageDraw.Draw(i) for i in imgs]
    for s in strokes:
        if len(s) == 0:
            continue
        p = s * k
        if len(p) == 1:
            x, y = p[0]
            draws[0].ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=255)
            continue
        for a, b in zip(p[:-1], p[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            if dx == 0 and dy == 0:
                continue
            xy = [float(a[0]), float(a[1]), float(b[0]), float(b[1])]
            draws[0].line(xy, fill=255, width=w)
            t = (math.atan2(dy, dx) % (2 * math.pi)) / (math.pi / 4)
            b0 = int(t) % 8
            f = t - int(t)
            for bi, wt in ((b0, 1 - f), ((b0 + 1) % 8, f)):
                if wt > 0.05:
                    draws[1 + bi].line(xy, fill=int(255 * wt), width=w)
    out = np.stack([np.asarray(i.reduce(ss), dtype=np.uint8) for i in imgs])
    return out


def prepare_sample(strokes, size=64, width=1.8):
    """The inference-time path for real ink (surface coordinates): drop specks, fit, render."""
    arrs = [np.asarray(s, dtype=np.float32).reshape(-1, 2) for s in strokes if len(s)]
    arrs = drop_specks(arrs)
    return render(fit_to_canvas(arrs), size=size, width=width)
