"""Stroke-space augmentation for the Tegaki templates (coordinates 0-1000 -> a 300 canvas).

Models what a trackpad does to a clean character: low-frequency wobble, per-point jitter,
per-stroke offsets/rotations, anisotropic scale, rotation, shear, cursive joins between strokes,
and stroke ends that under/overshoot. Stroke order is irrelevant to the renderer, so reordering
and merging are not simulated, except for visible joins.
"""
import math

import numpy as np


def densify(stroke, step):
    if len(stroke) < 2:
        return stroke.copy()
    seg = np.hypot(*(stroke[1:] - stroke[:-1]).T)
    total = float(seg.sum())
    if total < 1e-6:
        return stroke[:1].copy()
    n = max(2, int(total / step) + 1)
    arc = np.concatenate([[0.0], np.cumsum(seg)])
    t = np.linspace(0.0, total, n)
    return np.stack([np.interp(t, arc, stroke[:, 0]), np.interp(t, arc, stroke[:, 1])], axis=1)


def augment(strokes, rng, strength=1.0, level=1):
    """strokes: list of (N,2) arrays in 0-1000 template coordinates. Returns list in ~300 coords.

    level 2 ("loose"): per-sample strength drawn from 0.6-1.8x, rounder corners, more and curved
    cursive joins, occasional missing short strokes and retraced strokes, wider global distortion.
    """
    hard = level >= 2
    if hard:
        strength *= rng.uniform(0.6, 1.8)
    out = []
    for si, s in enumerate(strokes):
        if hard and len(strokes) > 2 and rng.random() < 0.04 and float(np.hypot(*np.diff(s * 0.3, axis=0).T).sum()) < 40:
            continue  # short strokes (dots, ticks) sometimes go missing

        s = densify(s * 0.3, step=rng.uniform(1.5, 3.5))
        # round off template corners like fast handwriting does (moving average, ends fixed)
        for _ in range(int(rng.integers(0, (13 if hard else 7) if rng.random() < (0.7 if hard else 0.6) else 1))):
            if len(s) > 4:
                s[1:-1] = (s[:-2] + s[1:-1] + s[2:]) / 3.0
        if len(s) >= 2:
            # per-stroke rigid wobble around its centre
            c = s.mean(axis=0)
            ang = math.radians(rng.normal(0, 2.0 * strength))
            ca, sa = math.cos(ang), math.sin(ang)
            r = np.array([[ca, -sa], [sa, ca]], dtype=np.float32)
            s = (s - c) @ r.T * rng.uniform(1 - 0.05 * strength, 1 + 0.05 * strength) + c
            s = s + rng.normal(0, 3.0 * strength, size=2)
            # low-frequency lateral wobble
            seg = np.hypot(*(s[1:] - s[:-1]).T)
            arc = np.concatenate([[0.0], np.cumsum(seg)])
            d = s[min(len(s) - 1, 1):] - s[:-1]
            tang = np.gradient(s, axis=0)
            nrm = np.stack([-tang[:, 1], tang[:, 0]], axis=1)
            nl = np.hypot(*nrm.T)[:, None]
            nrm = nrm / np.maximum(nl, 1e-6)
            amp = rng.uniform(0, (7.0 if hard else 5.0) * strength)
            wob = amp * np.sin(rng.uniform(0, 6.28) + rng.uniform(0.02, 0.06) * arc)
            s = s + nrm * wob[:, None]
            s = s + rng.normal(0, rng.uniform(0, 1.2 * strength), size=s.shape)
            # end under/overshoot
            if rng.random() < 0.15 and len(s) > 3:
                cut = int(len(s) * rng.uniform(0.0, 0.08))
                s = s[: len(s) - cut] if cut else s
            if rng.random() < 0.15:
                d_end = s[-1] - s[-3] if len(s) > 3 else np.zeros(2)
                n = np.hypot(*d_end)
                if n > 1e-6:
                    s = np.vstack([s, s[-1] + d_end / n * rng.uniform(3, 10)])
        out.append(s.astype(np.float32))

    # cursive joins: draw the pen travel between consecutive strokes
    for i in range(len(out) - 1, 0, -1):
        gap = np.hypot(*(out[i][0] - out[i - 1][-1]))
        if gap < (110 if hard else 70) and rng.random() < (0.25 if hard else 0.12) * strength:
            a, b = out[i - 1][-1], out[i][0]
            if hard and rng.random() < 0.6:  # curved connector
                mid = (a + b) / 2 + rng.normal(0, 0.25 * max(gap, 10), size=2)
                t = np.linspace(0, 1, 8)[:, None]
                conn = (1 - t) ** 2 * a + 2 * (1 - t) * t * mid + t ** 2 * b
            else:
                conn = np.stack([a, b])
            out.insert(i, conn.astype(np.float32))
    if hard and out and rng.random() < 0.03:  # retraced stroke
        k = int(rng.integers(len(out)))
        out.append((out[k] + rng.normal(0, 1.5, size=out[k].shape)).astype(np.float32))

    # global: anisotropic scale, rotation, shear
    lo, hi = (0.6, 1.4) if hard else (0.7, 1.3)
    sx, sy = rng.uniform(lo, hi), rng.uniform(lo, hi)
    ang = math.radians(float(np.clip(rng.normal(0, 5.0 * strength), -20 if hard else -14, 20 if hard else 14)))
    sh = rng.uniform(-0.15, 0.15) * strength
    m = np.array([[math.cos(ang), -math.sin(ang)], [math.sin(ang), math.cos(ang)]]) @ np.array([[sx, sh], [0, sy]])
    allp = np.concatenate(out)
    c = allp.mean(axis=0)
    return [((s - c) @ m.T + c).astype(np.float32) for s in out]
