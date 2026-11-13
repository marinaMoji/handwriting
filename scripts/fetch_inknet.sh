#!/usr/bin/env bash
# Put the InkNet weights (order-free neural net for hybrid recognition) at data/inknet.bin.
#
#   scripts/fetch_inknet.sh FILE          copy from a local file (e.g. ml/runs/base2/model.inknet)
#   INKNET_URL=https://... scripts/fetch_inknet.sh   download
#
# The expected sha256 is checked. The weights are trained on Tegaki templates only (LGPL-2.1
# lineage, no Hanzi Writer data; see docs/licensing/handwriting/README.md). To train your own:
# ml/README.md and scripts/export_weights.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/data/inknet.bin"
# base2: 20,000 steps, level-1 augmentation, Tegaki Japanese + Traditional Chinese templates.
EXPECTED_SHA256="${INKNET_SHA256:-9dd016879e27af8429e658488f6d001daae4566ce6f50c21c5e0e942e0f9f91d}"

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

mkdir -p "$ROOT/data"
tmp="$(mktemp "$ROOT/data/inknet.XXXXXX")"
trap 'rm -f "$tmp"' EXIT

if [[ $# -ge 1 ]]; then
  cp "$1" "$tmp"
elif [[ -n "${INKNET_URL:-}" ]]; then
  curl -fL --retry 3 -o "$tmp" "$INKNET_URL"
else
  echo "usage: $0 FILE   or   INKNET_URL=https://... $0" >&2
  echo "No public download location has been decided yet; copy the file from a machine that has it." >&2
  exit 2
fi

actual="$(sha256 "$tmp")"
if [[ "$actual" != "$EXPECTED_SHA256" ]]; then
  echo "sha256 mismatch: got $actual, expected $EXPECTED_SHA256" >&2
  echo "(set INKNET_SHA256 to accept different weights, e.g. your own training run)" >&2
  exit 1
fi
mv "$tmp" "$DEST"
trap - EXIT
echo "InkNet weights ready: $DEST"
