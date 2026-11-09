#!/usr/bin/env bash
# Download Unihan_IRGSources.txt (kTotalStrokes) from unicode.org into ./data/.
# Only needed to regenerate src/StrokeCountTable.inc (see scripts/gen_stroke_counts.py).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/data/Unihan_IRGSources.txt"
mkdir -p "$ROOT/data"
if [[ -f "$OUT" ]]; then echo "Using existing $OUT"; exit 0; fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fL --retry 3 -o "$TMP/Unihan.zip" https://www.unicode.org/Public/UCD/latest/ucd/Unihan.zip
unzip -o -q "$TMP/Unihan.zip" Unihan_IRGSources.txt -d "$TMP"
mv "$TMP/Unihan_IRGSources.txt" "$OUT"
echo "Wrote $OUT"
