#!/usr/bin/env bash
# Build + repair rgpot wheel so install is portable ($ORIGIN purelib peers).
# Usage: ./scripts/rgpot_build_wheel.sh
# Requires torch/metatomic for engine link; sets CMAKE_PREFIX_PATH from torch.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python -m pip install -U build meson ninja meson-python pybind11 numpy wheel 2>/dev/null || true
rm -rf dist build .mesonpy*
if python -c "import torch, metatomic.torch" 2>/dev/null; then
  export CMAKE_PREFIX_PATH="$(python -c 'import torch; print(torch.utils.cmake_prefix_path)')${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  export RGPOT_TORCH_MAJOR="$(python -c 'import torch; print(".".join(torch.__version__.split("+")[0].split(".")[:2]))')"
  python -m build --wheel --no-isolation
else
  python -m build --wheel
fi
WHL=$(ls -1 dist/rgpot-*.whl | head -1)
bash "$ROOT/scripts/rgpot_repair_wheel.sh" "$WHL"
echo "WHEEL_OK $WHL"
