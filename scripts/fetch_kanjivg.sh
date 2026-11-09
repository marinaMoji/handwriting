#!/usr/bin/env bash
# Download KanjiVG (stroke-order data, CC BY-SA 3.0, https://kanjivg.tagaini.net) into ./data/.
# Used only to generate synthetic evaluation ink; the data is never committed or redistributed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${KANJIVG_TAG:-r20260714}"
VER="${TAG#r}"
OUT="$ROOT/data/kanjivg.xml"
mkdir -p "$ROOT/data"
if [[ -f "$OUT" ]]; then
  echo "Using existing $OUT"
  exit 0
fi
URL="https://github.com/KanjiVG/kanjivg/releases/download/${TAG}/kanjivg-${VER}.xml.gz"
echo "Downloading $URL"
curl -fL --retry 3 -o "$OUT.gz" "$URL"
gunzip -f "$OUT.gz"
echo "Wrote $OUT"
