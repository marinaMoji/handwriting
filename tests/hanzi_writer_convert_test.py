#!/usr/bin/env python3
"""Smoke tests for Hanzi Writer → Zinnia conversion (no network)."""
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import hanzi_writer_to_zinnia as hwz  # noqa: E402


def expect(cond: bool, msg: str) -> None:
    print(("OK  " if cond else "FAIL ") + msg)
    if not cond:
        raise SystemExit(1)


def main() -> int:
    # Synthetic median resembling 日 (4 strokes) in Hanzi Writer coords.
    medians = [
        [[100, 800], [100, 100]],
        [[100, 800], [900, 800], [900, 100]],
        [[100, 450], [900, 450]],
        [[100, 100], [900, 100]],
    ]
    strokes = hwz.hzw_to_tomoe(medians)
    expect(len(strokes) == 4, "four strokes preserved")
    # Top of HZW y=900 maps near y=0 in Tomoe; bottom y=-124 maps near y=1000.
    expect(strokes[0][0][1] < 150, "top maps near y=0")
    expect(strokes[0][-1][1] > 700, "bottom maps toward y=1000")

    sexp = hwz.to_sexp("日", strokes)
    expect(sexp.startswith("(character (value 日)"), "sexp value")
    expect("(strokes " in sexp, "sexp strokes key")

    rng = __import__("random").Random(1)
    aug = hwz.augment(strokes, rng)
    expect(len(aug) == 4, "augmentation keeps stroke count")

    # Round-trip through a tiny JSON file like Hanzi Writer's layout.
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "日.json").write_text(
            json.dumps({"strokes": [], "medians": medians}, ensure_ascii=False),
            encoding="utf-8",
        )
        loaded = hwz.load_hzw(d, "日")
        expect(loaded is not None and len(loaded) == 4, "load_hzw reads medians")

    print("All hanzi_writer_convert tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
