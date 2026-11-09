#!/usr/bin/env python3
"""Fail when handwriting_eval's CSV falls below tests/eval_thresholds.json.

usage: check_eval_gate.py eval.csv [thresholds.json]
"""
import csv
import json
import sys

csv_path = sys.argv[1]
thr_path = sys.argv[2] if len(sys.argv) > 2 else "tests/eval_thresholds.json"
thr = json.load(open(thr_path, encoding="utf-8"))
rows = {(r["variant"], r["mode"]): r for r in csv.DictReader(open(csv_path, encoding="utf-8"))}

bad = 0
for mode, mins in thr["min"].items():
    row = rows.get((thr["variant"], mode))
    if row is None:
        print(f"MISSING  {thr['variant']} / {mode} not in {csv_path}")
        bad += 1
        continue
    for metric, floor in mins.items():
        got = float(row[metric])
        ok = got >= floor
        bad += not ok
        print(f"{'OK  ' if ok else 'FAIL'}  {mode:5} {metric}: {got:.1f} (min {floor:.1f})")
sys.exit(1 if bad else 0)
