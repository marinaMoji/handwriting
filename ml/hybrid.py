"""Fuse Zinnia candidates with InkNet log-probabilities and report metrics.

usage: python -m ml.hybrid zinnia_dump.json neural_dump.json [--groups DIR=label ...]

Zinnia lists come from `handwriting_eval --dump`, neural lists from `ml.eval_real --dump`.
Hybrid score of a character = neural_logprob + w * zinnia_score, with the worst value seen in
that list standing in when a character is missing from one of them. w is tuned on the even
code points and checked on the odd ones, and vice versa (no tuning on the half being reported).
"""
import json
import os
import sys

import numpy as np


def truth(name):
    # U+XXXX_timestamp.json
    return chr(int(name.split("_")[0][2:], 16))


def zinnia_both(entry):
    """ja and tw lists merged by best raw score (matches interleave within noise)."""
    best = {}
    for mode in ("ja", "tw"):
        for ch, sc in entry.get(mode, []):
            if ch not in best or sc > best[ch]:
                best[ch] = sc
    return best


def rank_of(scores, t):
    order = sorted(scores, key=lambda c: -scores[c])
    return order.index(t) + 1 if t in scores else 999


def hybrid(z, n, w):
    floor_z = min(z.values()) if z else 0.0
    floor_n = min(n.values()) if n else -20.0
    chars = set(z) | set(n)
    return {c: n.get(c, floor_n) + w * z.get(c, floor_z) for c in chars}


def metrics(ranks):
    n = max(len(ranks), 1)
    return [100.0 * sum(r <= k for r in ranks) / n for k in (1, 5, 10)]


def fmt(m):
    return f"top1 {m[0]:5.1f}  top5 {m[1]:5.1f}  top10 {m[2]:5.1f}"


def main():
    zd = json.load(open(sys.argv[1], encoding="utf-8"))
    nd = json.load(open(sys.argv[2], encoding="utf-8"))
    names = sorted(set(zd) & set(nd))
    groups = {}
    for a in sys.argv[3:]:
        if "=" in a:
            d, label = a.split("=", 1)
            for f in os.listdir(d):
                groups[f] = label
    Z = {k: zinnia_both(zd[k]) for k in names}
    N = {k: {c: s for c, s in nd[k]} for k in names}
    T = {k: truth(k) for k in names}
    half = lambda k: ord(T[k][0]) % 2

    def ranks(sel, fn):
        return [rank_of(fn(k), T[k]) for k in sel]

    print(f"samples: {len(names)}")
    print("Zinnia (both, best score):", fmt(metrics(ranks(names, lambda k: Z[k]))))
    print("InkNet alone:             ", fmt(metrics(ranks(names, lambda k: N[k]))))

    weights = [0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 1000]
    print("\nhybrid vs weight w (all samples; w=1000 ~ Zinnia only, 0 = InkNet only)")
    for w in weights:
        print(f"  w={w:<5}", fmt(metrics(ranks(names, lambda k: hybrid(Z[k], N[k], w)))))

    # cross-validated: choose w on one half by top-5 (ties: top-1), report the other half
    print("\ncross-validated (tune on one half, report the other):")
    pooled = []
    for tune_h in (0, 1):
        tune = [k for k in names if half(k) == tune_h]
        test = [k for k in names if half(k) != tune_h]
        best = max(weights[:-1], key=lambda w: tuple(metrics(ranks(tune, lambda k: hybrid(Z[k], N[k], w)))[1::-1]))
        r = ranks(test, lambda k: hybrid(Z[k], N[k], best))
        pooled += r
        base = ranks(test, lambda k: Z[k])
        print(f"  tuned w={best:<3} on half {tune_h}: test half {1-tune_h}: hybrid {fmt(metrics(r))} | Zinnia {fmt(metrics(base))}")
    print("  pooled hybrid:", fmt(metrics(pooled)))

    if groups:
        print("\nby group (hybrid at the pooled-best w, Zinnia, InkNet):")
        wbest = max(weights[:-1], key=lambda w: tuple(metrics(ranks(names, lambda k: hybrid(Z[k], N[k], w)))[1::-1]))
        for label in sorted(set(groups.values())):
            sel = [k for k in names if groups.get(k) == label]
            print(f"  {label:<11} n={len(sel):<3} hybrid(w={wbest}) {fmt(metrics(ranks(sel, lambda k: hybrid(Z[k], N[k], wbest))))}"
                  f" | Zinnia {fmt(metrics(ranks(sel, lambda k: Z[k])))} | InkNet {fmt(metrics(ranks(sel, lambda k: N[k])))}")


if __name__ == "__main__":
    main()
