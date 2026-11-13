#!/usr/bin/env python3
"""Generate docs/practice-sets.json: the prompt lists for the app's Practice mode.

Two sets are produced, both drawn from the characters of the Tegaki Japanese inventory
(the characters the recognizer can output) that are ideographs:

  core       the held-out core: tag "test" (saved in samples-test/, never tuned on). Written by
             every writer, so writers can be compared. Excludes every character of
             docs/sample-chars.json and of any local samples/ (the development alphabet), so it
             also measures how well the recognizer does on characters it was not tuned on.
  dev-extra  more development material: tag "dev" (samples/). Excludes the core.

Pools (so that the sets are not all rare characters, and cover traditional forms):
  joyo     Japanese joyo kanji (Unihan kJoyoKanji)
  trad     kyujitai / traditional forms: a jinmeiyo kanji, or a common character that has a
           kSimplifiedVariant
  other    other common ideographs (Unihan school grade, or newspaper frequency rank 1-4)
Within a pool, characters are drawn at random (fixed seed) with the number per stroke count
proportional to sqrt(characters available at that stroke count), so complex characters are
represented but the list is not dominated by them.

Optional --hanzi-dir marks which characters are in the Hanzi Writer data (analysis only; the
selection does not depend on it).

usage: make_practice_sets.py --unihan-zip Unihan.zip [--hanzi-dir DIR] [--out docs/practice-sets.json]
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ml.tegaki import load_templates  # noqa: E402

SEED = 20261113


def read_unihan(zf: zipfile.ZipFile, name: str, fields: set[str]) -> dict[str, dict[str, str]]:
    out: dict[str, dict[str, str]] = {}
    with zf.open(name) as f:
        for raw in f:
            line = raw.decode("utf-8")
            if line.startswith("#") or not line.strip():
                continue
            cp, field, value = line.rstrip("\n").split("\t", 2)
            if field in fields:
                out.setdefault(chr(int(cp[2:], 16)), {})[field] = value
    return out


def stratified(pool: list[str], strokes: dict[str, int], n: int, rng: random.Random) -> list[str]:
    bins: dict[int, list[str]] = {}
    for c in pool:
        bins.setdefault(strokes[c], []).append(c)
    weights = {b: math.sqrt(len(v)) for b, v in bins.items()}
    total = sum(weights.values())
    quota = {b: n * w / total for b, w in weights.items()}
    take = {b: min(len(bins[b]), int(q)) for b, q in quota.items()}
    rest = sorted(bins, key=lambda b: quota[b] - int(quota[b]), reverse=True)
    for b in rest:
        if sum(take.values()) >= n:
            break
        if take[b] < len(bins[b]):
            take[b] += 1
    chosen: list[str] = []
    for b in sorted(bins):
        v = sorted(bins[b])
        rng.shuffle(v)
        chosen += v[: take[b]]
    return chosen


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--unihan-zip", type=Path, required=True)
    ap.add_argument("--tegaki-xml", type=Path, default=ROOT / "models_dl/tegaki-zinnia-japanese-0.3/handwriting-ja.xml")
    ap.add_argument("--hanzi-dir", type=Path)
    ap.add_argument("--samples", type=Path, default=ROOT / "samples")
    ap.add_argument("--out", type=Path, default=ROOT / "docs/practice-sets.json")
    ap.add_argument("--core", type=int, default=150)
    ap.add_argument("--extra", type=int, default=300)
    args = ap.parse_args()

    with zipfile.ZipFile(args.unihan_zip) as zf:
        other = read_unihan(zf, "Unihan_OtherMappings.txt", {"kJoyoKanji", "kJinmeiyoKanji"})
        irg = read_unihan(zf, "Unihan_IRGSources.txt", {"kTotalStrokes"})
        var = read_unihan(zf, "Unihan_Variants.txt", {"kSimplifiedVariant"})
        dic = read_unihan(zf, "Unihan_DictionaryLikeData.txt", {"kFrequency", "kGradeLevel"})

    inventory = [c for c in load_templates([str(args.tegaki_xml)]) if len(c) == 1 and 0x4E00 <= ord(c) <= 0x9FFF]
    strokes = {}
    for c in inventory:
        v = irg.get(c, {}).get("kTotalStrokes")
        if v:
            strokes[c] = int(v.split()[0])
    inventory = [c for c in inventory if c in strokes]

    listed = json.loads((ROOT / "docs/sample-chars.json").read_text(encoding="utf-8"))
    dev_alphabet = {c for cat in listed.values() for c in cat["chars"]}
    n_local = 0
    if args.samples.is_dir():
        for p in args.samples.glob("*.json"):
            try:
                dev_alphabet.add(json.loads(p.read_text(encoding="utf-8"))["character"])
                n_local += 1
            except (OSError, ValueError, KeyError):
                pass

    def common(c: str) -> bool:
        d = dic.get(c, {})
        return "kGradeLevel" in d or ("kFrequency" in d and int(d["kFrequency"]) <= 4)

    def pool_of(c: str) -> str:
        if "kJoyoKanji" in other.get(c, {}):
            return "joyo"
        if "kJinmeiyoKanji" in other.get(c, {}) or ("kSimplifiedVariant" in var.get(c, {}) and common(c)):
            return "trad"
        if common(c):
            return "other"
        return ""

    pools: dict[str, list[str]] = {"joyo": [], "trad": [], "other": []}
    for c in inventory:
        p = pool_of(c)
        if p:
            pools[p].append(c)

    hzw = None
    if args.hanzi_dir:
        hzw = {p.stem for p in args.hanzi_dir.glob("*.json")}

    def draw(excluded: set[str], total: int, seed: int) -> list[str]:
        rng = random.Random(seed)
        share = {"joyo": round(total * 0.53), "trad": round(total * 0.30)}
        share["other"] = total - share["joyo"] - share["trad"]
        chosen: list[str] = []
        for name, n in share.items():
            avail = [c for c in pools[name] if c not in excluded and c not in chosen]
            chosen += stratified(avail, strokes, n, rng)
        random.Random(seed + 1).shuffle(chosen)
        return chosen

    core = draw(dev_alphabet, args.core, SEED)
    extra = draw(dev_alphabet | set(core), args.extra, SEED + 100)

    def describe(chars: list[str]) -> dict:
        info = {}
        for c in chars:
            d = {"strokes": strokes[c], "pool": pool_of(c)}
            if hzw is not None:
                d["hzw"] = c in hzw
            info[c] = d
        return info

    sets = {
        "core": {
            "label": "Held-out core (all writers)",
            "tag": "test",
            "description": "Same list for every writer; saved in samples-test/ and never tuned on.",
            "chars": core,
            "info": describe(core),
        },
        "dev-extra": {
            "label": "More development characters",
            "tag": "dev",
            "description": "Broader coverage for tuning and training experiments; saved in samples/.",
            "chars": extra,
            "info": describe(extra),
        },
    }
    out = {
        "version": 1,
        "generator": "scripts/make_practice_sets.py",
        "seed": SEED,
        "excluded_development_characters": len(dev_alphabet),
        "unihan": "Unihan.zip (kJoyoKanji, kJinmeiyoKanji, kSimplifiedVariant, kFrequency, kGradeLevel, kTotalStrokes)",
        "sets": sets,
    }
    args.out.write_text(json.dumps(out, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    for name, s in sets.items():
        info = s["info"].values()
        by_pool = {p: sum(1 for i in info if i["pool"] == p) for p in ("joyo", "trad", "other")}
        spread = sorted(i["strokes"] for i in info)
        extra_note = f", in Hanzi Writer data: {sum(1 for i in info if i.get('hzw'))}" if hzw is not None else ""
        print(f"{name}: {len(s['chars'])} chars, pools {by_pool}, strokes {spread[0]}..{spread[-1]} "
              f"(median {spread[len(spread)//2]}){extra_note}")
    print(f"excluded {len(dev_alphabet)} development characters ({n_local} local samples read)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
