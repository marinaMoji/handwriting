#!/usr/bin/env python3
"""Convert Hanzi Writer medians (+ Tegaki templates) into a Zinnia training file.

Hanzi Writer / Make Me a Hanzi graphics are under the Arphic Public License
(ARPHICPL.TXT). Tegaki/Tomoe XML is LGPL-2.1. Experimental models trained from
this mix must stay out of production until redistribution terms are settled;
see docs/hanzi-writer-experiment.md.

Coordinate systems
------------------
Hanzi Writer medians: 1024×1024 box, y decreases downward,
  upper-left ≈ (0, 900), lower-right ≈ (1024, -124).
Zinnia / Tomoe: width/height 1000, y increases downward, origin top-left.

Usage
-----
  python3 scripts/hanzi_writer_to_zinnia.py \\
    --hanzi-dir experiments/hanzi-writer-data/data \\
    --tegaki-xml models_dl/tegaki-zinnia-japanese-0.3/handwriting-ja.xml \\
    --out experiments/hanzi-writer-aug/train.ja.s \\
    --max-chars 800 --variants 4 --seed 20261113
"""
from __future__ import annotations

import argparse
import html
import json
import math
import os
import random
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from ml.tegaki import load_templates  # noqa: E402


def hzw_to_tomoe(medians: list) -> list[list[tuple[int, int]]]:
    """Map Hanzi Writer medians into 0–1000 Tomoe/Zinnia coordinates."""
    strokes = []
    for median in medians:
        pts = []
        for x, y in median:
            # y: 900 (top) → -124 (bottom)  →  0..1000 downward
            tx = int(round(float(x) / 1024.0 * 1000.0))
            ty = int(round((900.0 - float(y)) / 1024.0 * 1000.0))
            tx = max(0, min(1000, tx))
            ty = max(0, min(1000, ty))
            if not pts or pts[-1] != (tx, ty):
                pts.append((tx, ty))
        if len(pts) >= 2:
            strokes.append(pts)
        elif len(pts) == 1:
            # Degenerate: stretch to a tiny segment so Zinnia accepts it.
            x, y = pts[0]
            strokes.append([(x, y), (min(1000, x + 1), y)])
    return strokes


def resample(stroke: list[tuple[int, int]], step: float) -> list[tuple[int, int]]:
    if len(stroke) < 2:
        return stroke
    out = [stroke[0]]
    for i in range(1, len(stroke)):
        x0, y0 = out[-1]
        x1, y1 = stroke[i]
        dist = math.hypot(x1 - x0, y1 - y0)
        n = max(1, int(dist / step))
        for k in range(1, n + 1):
            t = k / n
            out.append((int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t))))
    # Dedup consecutive
    dedup = [out[0]]
    for p in out[1:]:
        if p != dedup[-1]:
            dedup.append(p)
    return dedup if len(dedup) >= 2 else out[:2]


def augment(
    strokes: list[list[tuple[int, int]]],
    rng: random.Random,
    *,
    wobble_amp: float = 8.0,
    jitter: float = 2.0,
    scale_range: tuple[float, float] = (0.85, 1.15),
    aspect_range: tuple[float, float] = (0.9, 1.1),
    rotate_deg: float = 5.0,
    step: float = 12.0,
) -> list[list[tuple[int, int]]]:
    """Mild trackpad-like distortion. Keeps stroke count and order."""
    sx = rng.uniform(*scale_range) * rng.uniform(*aspect_range)
    sy = rng.uniform(*scale_range)
    ang = math.radians(rng.uniform(-rotate_deg, rotate_deg))
    ca, sa = math.cos(ang), math.sin(ang)
    phase = rng.uniform(0, 6.28)
    freq = rng.uniform(0.015, 0.045)

    # Centroid
    pts_all = [p for s in strokes for p in s]
    cx = sum(p[0] for p in pts_all) / len(pts_all)
    cy = sum(p[1] for p in pts_all) / len(pts_all)

    out = []
    travelled = 0.0
    for stroke in strokes:
        dense = resample(stroke, step)
        new = []
        for i, (x, y) in enumerate(dense):
            # Translate to origin, scale/rotate, wobble along normal approx.
            dx, dy = x - cx, y - cy
            rx = dx * ca - dy * sa
            ry = dx * sa + dy * ca
            rx *= sx
            ry *= sy
            if i > 0:
                travelled += math.hypot(dense[i][0] - dense[i - 1][0], dense[i][1] - dense[i - 1][1])
            wob = wobble_amp * math.sin(phase + freq * travelled)
            # Approximate normal from previous segment
            if i > 0:
                vx = dense[i][0] - dense[i - 1][0]
                vy = dense[i][1] - dense[i - 1][1]
                ln = math.hypot(vx, vy) or 1.0
                nx, ny = -vy / ln, vx / ln
            else:
                nx, ny = 0.0, 0.0
            rx += nx * wob + rng.gauss(0, jitter)
            ry += ny * wob + rng.gauss(0, jitter)
            tx = int(round(rx + cx))
            ty = int(round(ry + cy))
            tx = max(0, min(1000, tx))
            ty = max(0, min(1000, ty))
            if not new or new[-1] != (tx, ty):
                new.append((tx, ty))
        if len(new) >= 2:
            out.append(new)
        elif new:
            x, y = new[0]
            out.append([(x, y), (min(1000, x + 1), y)])
    return out


def to_sexp(ch: str, strokes: list[list[tuple[int, int]]], width: int = 1000, height: int = 1000) -> str:
    # Escape characters that break the S-expression atom (parens, spaces).
    if ch in "() \t\n":
        raise ValueError(f"unsupported character for sexp: {ch!r}")
    parts = []
    for stroke in strokes:
        pts = "".join(f"({x} {y})" for x, y in stroke)
        parts.append(f"({pts})")
    return f"(character (value {ch})(width {width})(height {height})(strokes {' '.join(parts)}))"


def tegaki_chars(xml_path: Path) -> dict[str, list]:
    return load_templates([str(xml_path)])


def list_hzw_chars(data_dir: Path) -> set[str]:
    return {p.stem for p in data_dir.glob("*.json") if len(p.stem) == 1 or len(p.stem) <= 2}


def load_hzw(data_dir: Path, ch: str) -> list[list[tuple[int, int]]] | None:
    path = data_dir / f"{ch}.json"
    if not path.exists():
        # Try UTF-8 filename variants — already stem
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    medians = data.get("medians")
    if not medians:
        return None
    return hzw_to_tomoe(medians)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hanzi-dir", type=Path, required=True)
    ap.add_argument("--tegaki-xml", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument(
        "--max-chars",
        type=int,
        default=800,
        help="Max Hanzi Writer characters to add (0 = all Tegaki∩HWZ overlap, sorted by code point)",
    )
    ap.add_argument("--variants", type=int, default=4, help="Extra augmented copies per character (legacy mode)")
    ap.add_argument(
        "--hzw-mode",
        choices=("clean", "aug", "clean+aug"),
        default="clean+aug",
        help="clean: 1 clean median/char; aug: 1 augmented median/char; "
        "clean+aug: 1 clean + --variants augs (unequal count unless variants=0)",
    )
    ap.add_argument("--seed", type=int, default=20261113)
    ap.add_argument(
        "--prefer-sample-chars",
        type=Path,
        default=None,
        help="Optional samples/ dir: prefer characters that appear in real eval samples "
        "(omit for evaluation-independent selection)",
    )
    ap.add_argument("--tegaki-only", action="store_true", help="Baseline: emit Tegaki templates only")
    ap.add_argument(
        "--no-tegaki",
        action="store_true",
        help="Omit Tegaki templates (Hanzi Writer lines only; for a separate recognizer)",
    )
    args = ap.parse_args()
    if args.tegaki_only and args.no_tegaki:
        print("error: --tegaki-only and --no-tegaki conflict", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    tegaki = tegaki_chars(args.tegaki_xml)
    hzw_available = list_hzw_chars(args.hanzi_dir)

    sample_chars: set[str] = set()
    sample_dirs = []
    if args.prefer_sample_chars and args.prefer_sample_chars.is_dir():
        sample_dirs.append(args.prefer_sample_chars)
    # Always note coverage of local samples/ when present (stats only).
    default_samples = ROOT / "samples"
    if default_samples.is_dir() and default_samples not in sample_dirs:
        sample_dirs.append(default_samples)
    for d in sample_dirs:
        for f in d.glob("*.json"):
            try:
                sample_chars.add(json.loads(f.read_text(encoding="utf-8"))["character"])
            except (json.JSONDecodeError, KeyError, OSError):
                pass

    # Stable, evaluation-independent order: Unicode code point.
    overlap = sorted((ch for ch in tegaki if ch in hzw_available), key=lambda c: ord(c[0]))
    if args.max_chars == 0:
        selected = overlap
    elif args.prefer_sample_chars:
        prefer = [c for c in overlap if c in sample_chars]
        rest = [c for c in overlap if c not in sample_chars]
        rng.shuffle(rest)
        selected = (prefer + rest)[: args.max_chars]
    else:
        selected = overlap[: args.max_chars]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    n_lines = 0
    n_hzw = 0
    n_hzw_clean = 0
    n_hzw_aug = 0
    with args.out.open("w", encoding="utf-8") as out:
        # Full Tegaki inventory unless --no-tegaki (separate HZW recognizer).
        if not args.no_tegaki:
            for ch, variants in tegaki.items():
                for strokes_np in variants:
                    strokes = [[(int(p[0]), int(p[1])) for p in s] for s in strokes_np]
                    out.write(to_sexp(ch, strokes) + "\n")
                    n_lines += 1

        if not args.tegaki_only:
            for ch in selected:
                clean = load_hzw(args.hanzi_dir, ch)
                if not clean:
                    continue
                if args.hzw_mode in ("clean", "clean+aug"):
                    out.write(to_sexp(ch, clean) + "\n")
                    n_lines += 1
                    n_hzw += 1
                    n_hzw_clean += 1
                if args.hzw_mode == "aug":
                    aug = augment(clean, rng)
                    if aug:
                        out.write(to_sexp(ch, aug) + "\n")
                        n_lines += 1
                        n_hzw += 1
                        n_hzw_aug += 1
                elif args.hzw_mode == "clean+aug":
                    for _ in range(args.variants):
                        aug = augment(clean, rng)
                        if aug:
                            out.write(to_sexp(ch, aug) + "\n")
                            n_lines += 1
                            n_hzw += 1
                            n_hzw_aug += 1

    meta = {
        "seed": args.seed,
        "max_chars": args.max_chars,
        "variants": args.variants,
        "hzw_mode": args.hzw_mode,
        "selection": "all_overlap_codepoint"
        if args.max_chars == 0
        else ("prefer_samples" if args.prefer_sample_chars else "prefix_codepoint"),
        "tegaki_xml": str(args.tegaki_xml),
        "hanzi_dir": str(args.hanzi_dir),
        "selected_chars": len(selected),
        "selected_from_samples": sum(1 for c in selected if c in sample_chars),
        "overlap_available": len(overlap),
        "tegaki_characters": len(tegaki),
        "train_lines": n_lines,
        "hanzi_writer_lines": n_hzw,
        "hanzi_writer_clean_lines": n_hzw_clean,
        "hanzi_writer_aug_lines": n_hzw_aug,
        "tegaki_only": args.tegaki_only,
        "no_tegaki": args.no_tegaki,
        "augmentation": {
            "wobble_amp": 8.0,
            "jitter": 2.0,
            "scale_range": [0.85, 1.15],
            "aspect_range": [0.9, 1.1],
            "rotate_deg": 5.0,
            "step": 12.0,
        },
        "licence_note": "Hanzi Writer graphics: Arphic Public License; Tegaki XML: LGPL-2.1",
    }
    meta_path = args.out.with_suffix(args.out.suffix + ".meta.json")
    meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(meta, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
