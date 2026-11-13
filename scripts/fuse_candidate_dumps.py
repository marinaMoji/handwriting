#!/usr/bin/env python3
"""Fuse two handwriting_eval --dump JSON files (ja lists) and score top-N.

Used to compare a *fused* single Zinnia model against *separate* recognizers
(production Tegaki + HZW-only) whose candidate lists are merged offline.

Dump format (from handwriting_eval --dump): { sampleName: { "ja": [[char, score], ...] } }
Ground truth: character field of samples/<sampleName>.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def load_truth(samples_dir: Path) -> dict[str, str]:
    out = {}
    for f in samples_dir.glob("*.json"):
        out[f.name] = json.loads(f.read_text(encoding="utf-8"))["character"]
    return out


def ranks(cands: list) -> dict[str, int]:
    return {c: i + 1 for i, (c, _) in enumerate(cands)}


def fuse(a: list, b: list, method: str, top_n: int, rrf_k: float = 10.0) -> list[str]:
    """Return ordered character list."""
    if method == "interleave":
        seen, out = set(), []
        for i in range(max(len(a), len(b))):
            for lst in (a, b):
                if i < len(lst):
                    ch = lst[i][0]
                    if ch not in seen:
                        seen.add(ch)
                        out.append(ch)
                        if len(out) >= top_n:
                            return out
        return out

    ra, rb = ranks(a), ranks(b)
    score: dict[str, float] = {}
    best: dict[str, float] = {}
    for ch, sc in a:
        best[ch] = sc
        if method == "rrf":
            score[ch] = score.get(ch, 0.0) + 1.0 / (rrf_k + ra[ch])
        elif method == "max":
            score[ch] = sc
    for ch, sc in b:
        best[ch] = max(best.get(ch, -1e9), sc)
        if method == "rrf":
            score[ch] = score.get(ch, 0.0) + 1.0 / (rrf_k + rb[ch])
        elif method == "max":
            score[ch] = max(score.get(ch, -1e9), sc)

    if method == "z":
        def zmap(lst):
            if not lst:
                return {}
            vals = [s for _, s in lst]
            mean = sum(vals) / len(vals)
            var = sum((s - mean) ** 2 for s in vals) / len(vals)
            sd = math.sqrt(var) or 1.0
            return {c: (s - mean) / sd for c, s in lst}

        za, zb = zmap(a), zmap(b)
        chars = set(za) | set(zb)
        for ch in chars:
            score[ch] = max(za.get(ch, -1e9), zb.get(ch, -1e9))

    ordered = sorted(score.keys(), key=lambda c: (-score[c], c))
    return ordered[:top_n]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--a", type=Path, required=True, help="First dump JSON (e.g. production)")
    ap.add_argument("--b", type=Path, required=True, help="Second dump JSON (e.g. hzw-only)")
    ap.add_argument("--samples", type=Path, default=Path("samples"))
    ap.add_argument("--key", default="ja", help="List key inside each sample object")
    ap.add_argument("--methods", default="interleave,rrf,max,z")
    ap.add_argument("--top-n", type=int, default=10)
    ap.add_argument("--rrf-k", type=float, default=10.0)
    args = ap.parse_args()

    da = json.loads(args.a.read_text(encoding="utf-8"))
    db = json.loads(args.b.read_text(encoding="utf-8"))
    truth = load_truth(args.samples)
    names = sorted(set(da) & set(db) & set(truth))

    print(f"samples scored: {len(names)} (intersection of dumps + truth)")
    print(f"{'method':12} {'top1':>7} {'top5':>7} {'top10':>7} {'mrr':>7}")
    for method in args.methods.split(","):
        method = method.strip()
        t1 = t5 = t10 = 0
        rr = 0.0
        for name in names:
            gt = truth[name]
            la = da[name].get(args.key, [])
            lb = db[name].get(args.key, [])
            order = fuse(la, lb, method, args.top_n, args.rrf_k)
            if gt in order:
                rank = order.index(gt) + 1
                rr += 1.0 / rank
                if rank == 1:
                    t1 += 1
                if rank <= 5:
                    t5 += 1
                if rank <= 10:
                    t10 += 1
        n = max(1, len(names))
        print(
            f"{method:12} {100*t1/n:6.1f} {100*t5/n:6.1f} {100*t10/n:6.1f} {rr/n:6.3f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
