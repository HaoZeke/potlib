#!/usr/bin/env bash
# Build + multi-ABI engine layout + RPATH repair.
# Nanobind abi3 when build Python >= 3.12.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python -m pip install -U build meson ninja meson-python nanobind numpy wheel 2>/dev/null || true
rm -rf dist build .mesonpy*
if python -c "import torch, metatomic.torch" 2>/dev/null; then
  export CMAKE_PREFIX_PATH="$(python -c 'import torch; print(torch.utils.cmake_prefix_path)')${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  export RGPOT_TORCH_MAJOR="$(python -c 'import torch; print(".".join(torch.__version__.split("+")[0].split(".")[:2]))')"
  python -m build --wheel --no-isolation
else
  python -m build --wheel
fi
WHL=$(ls -1 dist/rgpot-*.whl | head -1)
bash "$ROOT/scripts/rgpot_finalize_wheel.sh" "$WHL"
echo "WHEEL_OK $WHL"
