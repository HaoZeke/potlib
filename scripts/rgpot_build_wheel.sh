#!/usr/bin/env bash
# Build nanobind abi3 wheel + multi-torch engines + RPATH repair.
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
if [[ "${RGPOT_SKIP_MULTI_ABI:-0}" != "1" ]]; then
  bash "$ROOT/scripts/rgpot_build_multi_abi_engines.sh"
  bash "$ROOT/scripts/rgpot_inject_multi_abi_engines.sh" "$WHL"
fi
echo "WHEEL_OK $WHL"
ls -la "$ROOT/build/multi-abi-engines"/torch-*/libmetatomic_engine.so 2>/dev/null || true
