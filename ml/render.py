"""Order-free rendering of ink: fit like the C++ recognizer, then draw ink + 8 direction channels.

Channel 0 is the ink; channels 1..8 hold the same ink split by stroke direction (soft-assigned
to the two nearest of 8 direction bins). Stroke order and count are not represented.
"""
import math

import numpy as np

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


GRID = 64
SPACING = 0.5      # grid pixels between splatted points
OFFSETS = range(-2, 3)


def _resample(stroke, spacing):
    """Points along a stroke at (at most) `spacing` apart, endpoints included."""
    seg = np.hypot(*(stroke[1:] - stroke[:-1]).T) if len(stroke) > 1 else np.zeros(0)
    total = float(seg.sum())
    if total < 1e-6:
        return stroke[:1].astype(np.float64)
    m = max(2, int(math.ceil(total / spacing)) + 1)
    arc = np.concatenate([[0.0], np.cumsum(seg)])
    t = np.linspace(0.0, total, m)
    return np.stack([np.interp(t, arc, stroke[:, 0]), np.interp(t, arc, stroke[:, 1])], axis=1)


def render(strokes, size=GRID, width=1.8):
    """Order-free 9-channel render by dense point splatting. strokes are in the 300 canvas.

    Spec shared with the C++ port (src/InkRender.cpp):
      * scale 300 -> size; resample every stroke every <= 0.5 grid px (endpoints kept)
      * each point splats a tent kernel of radius R = width/2 + 0.5 onto pixel centres
        (ix + 0.5, iy + 0.5); weight g = 0.5 / R keeps a line's centre near 1
      * channel 0 gets the ink; the segment direction (to the next point, the last point uses
        the previous segment) is quantised to 8 bins and split between the two nearest bins
      * values saturate at 1 and are stored as round(255 * v)
    Returns uint8 (9, size, size).
    """
    k = size / CANVAS
    R = width / 2.0 + 0.5
    g = SPACING / R
    xs, ys, chans, vals = [], [], [], []
    pts_all, ink_only = [], []
    for s in strokes:
        if len(s) == 0:
            continue
        p = _resample(np.asarray(s, dtype=np.float64) * k, SPACING)
        if len(p) == 1:
            pts_all.append((p, None))
            continue
        d = np.empty_like(p)
        d[:-1] = p[1:] - p[:-1]
        d[-1] = d[-2]
        theta = np.arctan2(d[:, 1], d[:, 0]) % (2 * math.pi)
        t = theta / (math.pi / 4)
        b0 = np.floor(t).astype(np.int64)
        f = t - b0
        pts_all.append((p, (b0 % 8, f)))
    parts_idx, parts_val = [], []
    for p, dirinfo in pts_all:
        x, y = p[:, 0], p[:, 1]
        ix0, iy0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
        for oy in OFFSETS:
            for ox in OFFSETS:
                ix, iy = ix0 + ox, iy0 + oy
                d = np.hypot(ix + 0.5 - x, iy + 0.5 - y)
                w = np.maximum(0.0, 1.0 - d / R) * g
                ok = (w > 0) & (ix >= 0) & (ix < size) & (iy >= 0) & (iy < size)
                if not ok.any():
                    continue
                pix = (iy * size + ix)[ok]
                wv = w[ok]
                parts_idx.append(pix)
                parts_val.append(wv)
                if dirinfo is not None:
                    b, f = dirinfo
                    parts_idx.append(((1 + b[ok]) * size * size) + pix)
                    parts_val.append(wv * (1 - f[ok]))
                    parts_idx.append(((1 + (b[ok] + 1) % 8) * size * size) + pix)
                    parts_val.append(wv * f[ok])
    acc = np.zeros(NUM_CHANNELS * size * size)
    if parts_idx:
        acc = np.bincount(np.concatenate(parts_idx), weights=np.concatenate(parts_val),
                          minlength=NUM_CHANNELS * size * size)
    return np.rint(np.minimum(acc, 1.0) * 255).astype(np.uint8).reshape(NUM_CHANNELS, size, size)


def prepare_sample(strokes, size=64, width=1.8):
    """The inference-time path for real ink (surface coordinates): drop specks, fit, render."""
    arrs = [np.asarray(s, dtype=np.float32).reshape(-1, 2) for s in strokes if len(s)]
    arrs = drop_specks(arrs)
    return render(fit_to_canvas(arrs), size=size, width=width)
