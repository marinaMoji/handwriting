#!/usr/bin/env python3
"""Flag saved samples whose stroke order or count differs from KanjiVG.

usage: check_stroke_order.py [samples_dir] [--kanjivg data/kanjivg.xml] [--delete-flagged]
Both ink and template are normalised to their bounding boxes, each stroke is resampled to
8 points, and strokes are matched by direction-aware distance. Reports:
  COUNT   different number of strokes than KanjiVG (merged, split or missing strokes)
  ORDER   same count, but the best matching is not the identity (strokes written out of order)
Characters KanjiVG does not know are skipped. It is a heuristic: legitimate variants in
stroke order exist, so look at the flagged ones before discarding anything.
"""
import glob, itertools, json, math, os, re, sys
import xml.etree.ElementTree as ET

args = [a for a in sys.argv[1:] if not a.startswith("--")]
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
samples_dir = args[0] if args else os.path.join(root, "samples")
kvg_path = os.path.join(root, "data", "kanjivg.xml")
if "--kanjivg" in sys.argv:
    kvg_path = sys.argv[sys.argv.index("--kanjivg") + 1]

TOK = re.compile(r"[A-Za-z]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")

def flatten(d, seg=8):
    t = TOK.findall(d); i = 0; cmd = None
    cx = cy = sx = sy = 0.0; c2 = None; pts = []
    def num():
        nonlocal i
        if i >= len(t) or t[i].isalpha(): raise ValueError
        i += 1; return float(t[i - 1])
    try:
        while i < len(t):
            if t[i].isalpha(): cmd = t[i]; i += 1
            if cmd is None: return []
            rel = cmd.islower(); c = cmd.upper()
            if c == "Z": cx, cy = sx, sy; c2 = None; continue
            if c in "ML":
                x, y = num(), num()
                cx, cy = (cx + x, cy + y) if rel else (x, y)
                if c == "M": sx, sy = cx, cy; cmd = "l" if rel else "L"
                pts.append((cx, cy)); c2 = None
            elif c == "H": x = num(); cx = cx + x if rel else x; pts.append((cx, cy)); c2 = None
            elif c == "V": y = num(); cy = cy + y if rel else y; pts.append((cx, cy)); c2 = None
            elif c in "CS":
                if c == "C":
                    v = [num() for _ in range(6)]
                    ox, oy = (cx, cy) if rel else (0, 0)
                    x1, y1, x2, y2, x, y = v[0]+ox, v[1]+oy, v[2]+ox, v[3]+oy, v[4]+ox, v[5]+oy
                else:
                    v = [num() for _ in range(4)]
                    ox, oy = (cx, cy) if rel else (0, 0)
                    x1, y1 = (2*cx - c2[0], 2*cy - c2[1]) if c2 else (cx, cy)
                    x2, y2, x, y = v[0]+ox, v[1]+oy, v[2]+ox, v[3]+oy
                for k in range(1, seg + 1):
                    s = k / seg; u = 1 - s
                    pts.append((u**3*cx + 3*u*u*s*x1 + 3*u*s*s*x2 + s**3*x,
                                u**3*cy + 3*u*u*s*y1 + 3*u*s*s*y2 + s**3*y))
                c2 = (x2, y2); cx, cy = x, y
            else: break
    except ValueError:
        pass
    return pts

def load_kvg(path):
    out = {}
    for _, el in ET.iterparse(path):
        if el.tag == "kanji":
            m = re.fullmatch(r"kvg:kanji_([0-9a-f]{5})", el.get("id", ""))
            if m:
                strokes = [flatten(p.get("d", "")) for p in el.iter("path")]
                strokes = [s for s in strokes if len(s) >= 2]
                if strokes: out[chr(int(m.group(1), 16))] = strokes
            el.clear()
    return out

def resample(s, n=8):
    d = [0.0]
    for a, b in zip(s, s[1:]): d.append(d[-1] + math.dist(a, b))
    if d[-1] == 0: return [s[0]] * n
    out = []
    for k in range(n):
        target = d[-1] * k / (n - 1); j = 1
        while j < len(d) - 1 and d[j] < target: j += 1
        seg = d[j] - d[j-1] or 1.0; f = (target - d[j-1]) / seg
        out.append((s[j-1][0] + (s[j][0]-s[j-1][0])*f, s[j-1][1] + (s[j][1]-s[j-1][1])*f))
    return out

def normalise(strokes):
    xs = [p[0] for s in strokes for p in s]; ys = [p[1] for s in strokes for p in s]
    w = (max(xs) - min(xs)) or 1.0; h = (max(ys) - min(ys)) or 1.0
    k = max(w, h)  # keep aspect, centre
    ox = (min(xs) + max(xs)) / 2; oy = (min(ys) + max(ys)) / 2
    return [resample([((x-ox)/k, (y-oy)/k) for x, y in s]) for s in strokes]

def dist(a, b): return sum(math.dist(p, q) for p, q in zip(a, b)) / len(a)

def best_matching(cost):
    n = len(cost)
    if n <= 7:
        best = min(itertools.permutations(range(n)), key=lambda p: sum(cost[i][p[i]] for i in range(n)))
        return list(best)
    used = set(); res = []
    for i in range(n):  # greedy for large stroke counts
        j = min((j for j in range(n) if j not in used), key=lambda j: cost[i][j])
        used.add(j); res.append(j)
    return res

if not os.path.exists(kvg_path):
    sys.exit(f"{kvg_path} missing: run scripts/fetch_kanjivg.sh")
kvg = load_kvg(kvg_path)
flagged = []; checked = 0
for path in sorted(glob.glob(os.path.join(samples_dir, "**", "*.json"), recursive=True)):
    try: s = json.load(open(path, encoding="utf-8"))
    except (OSError, ValueError): continue
    ch = s.get("character", ""); strokes = [[tuple(p) for p in st] for st in s.get("strokes", []) if len(st) >= 2]
    tpl = kvg.get(ch)
    if not tpl or not strokes: continue
    checked += 1
    if len(strokes) != len(tpl):
        flagged.append((path, ch, f"COUNT   wrote {len(strokes)} strokes, KanjiVG has {len(tpl)}")); continue
    a = normalise(strokes); b = normalise(tpl)
    n = len(a); cost = [[dist(a[i], b[j]) for j in range(n)] for i in range(n)]
    match = best_matching(cost)
    ident = sum(cost[i][i] for i in range(n)); best = sum(cost[i][match[i]] for i in range(n))
    if match != list(range(n)) and ident - best > 0.02 * n:
        moves = ", ".join(f"your #{i+1} is KanjiVG #{match[i]+1}" for i in range(n) if match[i] != i)
        flagged.append((path, ch, f"ORDER   {moves}"))
print(f"checked {checked} samples against KanjiVG; flagged {len(flagged)}\n")
for path, ch, why in flagged:
    print(f"{ch}  {os.path.basename(path)}  {why}")
