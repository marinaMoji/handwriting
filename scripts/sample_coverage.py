#!/usr/bin/env python3
"""Show collection progress against docs/sample-chars.json.

usage: sample_coverage.py [samples_dir] [--quick N] [--writer ID]
Counts saved samples per character, per category, and lists what is still missing.
A character is "done" with >= 1 sample (>= 2 if --quick 2 is given, for a second style).
"""
import glob
import json
import os
import sys

argv = sys.argv[1:]
writer = None
if "--writer" in argv:
    i = argv.index("--writer")
    writer = argv[i + 1]
    del argv[i : i + 2]
args = [a for a in argv if not a.startswith("--")]
need = 2 if "--quick" in sys.argv else 1
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
samples_dir = args[0] if args else os.path.join(root, "samples")
cats = json.load(open(os.path.join(root, "docs", "sample-chars.json"), encoding="utf-8"))

counts = {}
for path in glob.glob(os.path.join(samples_dir, "**", "*.json"), recursive=True):
    try:
        d = json.load(open(path, encoding="utf-8"))
        if writer is not None and d.get("writer") != writer:
            continue
        ch = d.get("character", "")
    except (OSError, ValueError):
        continue
    if ch:
        counts[ch] = counts.get(ch, 0) + 1

total = 0
listed = set()
for key, cat in cats.items():
    chars = cat["chars"]
    listed.update(chars)
    done = sum(1 for c in chars if counts.get(c, 0) >= need)
    n = sum(counts.get(c, 0) for c in chars)
    total += n
    missing = "".join(c for c in chars if counts.get(c, 0) < need)
    print(f"{cat['label']:<38} {done:>3}/{len(chars):<3} chars  {n:>3} samples  missing: {missing or '-'}")
extra = sum(n for c, n in counts.items() if c not in listed)
print(f"\ntotal on the list: {total} samples; other characters: {extra}")
