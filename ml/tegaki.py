"""Load the Tegaki/Tomoe training templates shipped inside the Zinnia model tarballs.

Files: models_dl/tegaki-zinnia-*/handwriting-*.xml (LGPL-2.1, coordinates 0-1000, y down).
Run scripts/fetch_models.sh first.
"""
import glob
import html
import os
import re

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def default_paths():
    return sorted(glob.glob(os.path.join(ROOT, "models_dl", "tegaki-zinnia-*", "handwriting-*.xml")))


def load_templates(paths=None):
    """Return {character: [strokes, ...]} where strokes is a list of float32 arrays (N, 2)."""
    paths = paths or default_paths()
    if not paths:
        raise FileNotFoundError("no handwriting-*.xml found in models_dl/ (run scripts/fetch_models.sh)")
    out = {}
    for p in paths:
        text = open(p, encoding="utf-8").read()
        for m in re.finditer(r"<character>(.*?)</character>", text, re.S):
            body = m.group(1)
            u = re.search(r"<utf8>(.*?)</utf8>", body, re.S)
            if not u:
                continue
            ch = html.unescape(u.group(1).strip())
            strokes = []
            for sm in re.finditer(r"<stroke>(.*?)</stroke>", body, re.S):
                pts = re.findall(r'<point x="(-?\d+)" y="(-?\d+)"', sm.group(1))
                if pts:
                    strokes.append(np.array(pts, dtype=np.float32))
            if strokes:
                out.setdefault(ch, []).append(strokes)
    return out
