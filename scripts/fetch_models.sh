#!/usr/bin/env bash
# Download Tegaki Zinnia Japanese and Traditional Chinese models into ./models/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODELS="$ROOT/models"
CACHE="$ROOT/models_dl"
mkdir -p "$MODELS" "$CACHE"

JA_URL="${JA_URL:-http://archive.ubuntu.com/ubuntu/pool/universe/t/tegaki-zinnia-japanese/tegaki-zinnia-japanese_0.3.orig.tar.gz}"
TW_URL="${TW_URL:-http://archive.ubuntu.com/ubuntu/pool/universe/t/tegaki-zinnia-traditional-chinese/tegaki-zinnia-traditional-chinese_0.3.orig.tar.gz}"

JA_TAR="$CACHE/tegaki-zinnia-japanese_0.3.orig.tar.gz"
TW_TAR="$CACHE/tegaki-zinnia-traditional-chinese_0.3.orig.tar.gz"

download() {
  local url="$1"
  local out="$2"
  if [[ -f "$out" ]]; then
    echo "Using cached $(basename "$out")"
    return
  fi
  echo "Downloading $url"
  curl -fL --retry 3 -o "$out" "$url"
}

download "$JA_URL" "$JA_TAR"
download "$TW_URL" "$TW_TAR"

tmp="$CACHE/extract"
rm -rf "$tmp"
mkdir -p "$tmp"
tar -xzf "$JA_TAR" -C "$tmp"
tar -xzf "$TW_TAR" -C "$tmp"

cp "$tmp"/tegaki-zinnia-japanese-0.3/handwriting-ja.model "$MODELS/"
cp "$tmp"/tegaki-zinnia-japanese-0.3/handwriting-ja.meta "$MODELS/"
cp "$tmp"/tegaki-zinnia-traditional-chinese-0.3/handwriting-zh_TW.model "$MODELS/"
cp "$tmp"/tegaki-zinnia-traditional-chinese-0.3/handwriting-zh_TW.meta "$MODELS/"

# Keep license texts next to download instructions (not the bulky models).
cp "$tmp"/tegaki-zinnia-japanese-0.3/COPYING "$MODELS/COPYING.japanese-model"
cp "$tmp"/tegaki-zinnia-traditional-chinese-0.3/COPYING "$MODELS/COPYING.traditional-chinese-model"

echo "Models ready in $MODELS"
ls -lh "$MODELS"/*.model
