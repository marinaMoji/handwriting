#!/usr/bin/env bash
# Export a trained run to the flat file the C++ runtime loads and copy it to data/inknet.bin,
# the default location the CLI and eval tool look for.
# usage: scripts/export_weights.sh RUN_NAME
set -euo pipefail
cd "$(dirname "$0")/.."
run="${1:?usage: export_weights.sh RUN_NAME}"
.venv-ml/bin/python -m ml.export "ml/runs/$run" "ml/runs/$run/model.inknet"
mkdir -p data
cp "ml/runs/$run/model.inknet" data/inknet.bin
echo "default weights: data/inknet.bin (from $run)"
