#!/usr/bin/env bash
# Rewrite absolute purelib RUNPATHs to $ORIGIN-relative site-packages paths.
#
# metatensor-torch / metatomic-torch ship *every* torch-X.Y ABI dir in one
# install. RUNPATH is searched in order and the first existing dir that
# contains the SONAME wins — listing 2.3..2.15 therefore loads the wrong ABI
# (undefined symbols). Keep a single torch major: the one the engine was
# linked against (from prior absolute RUNPATH, DT_NEEDED layout, or env).
set -euo pipefail
WHL="${1:?wheel path}"
command -v patchelf >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
python3 -m zipfile -e "$WHL" "$WORK"

ENG_SO=$(find "$WORK" -name 'libmetatomic_engine.so' -print -quit)
TORCH_MAJ="${RGPOT_TORCH_MAJOR:-}"
if [[ -z "$TORCH_MAJ" && -n "$ENG_SO" ]]; then
  # Prefer torch-X.Y already present in the engine RUNPATH from the build.
  TORCH_MAJ=$(readelf -d "$ENG_SO" 2>/dev/null \
    | sed -n 's/.*torch-\([0-9]\+\.[0-9]\+\).*/\1/p' | head -1 || true)
fi
if [[ -z "$TORCH_MAJ" ]]; then
  TORCH_MAJ=$(python3 -c 'import torch; print(".".join(torch.__version__.split("+")[0].split(".")[:2]))' 2>/dev/null || true)
fi
TORCH_MAJ=${TORCH_MAJ:-2.9}
echo "repair torch ABI major: $TORCH_MAJ"

# Peer paths from .rgpot.mesonpy.libs/ — single ABI only (see header comment).
ENG_RPATH="\$ORIGIN:\$ORIGIN/../torch/lib:\$ORIGIN/../metatensor/lib:\$ORIGIN/../vesin/lib"
ENG_RPATH+=":\$ORIGIN/../metatensor_torch/torch-${TORCH_MAJ}/lib"
ENG_RPATH+=":\$ORIGIN/../metatomic/torch/torch-${TORCH_MAJ}/lib"
ENG_RPATH+=":\$ORIGIN/../metatensor/torch/torch-${TORCH_MAJ}/lib"
CORE_RPATH='$ORIGIN:$ORIGIN/../.rgpot.mesonpy.libs'
POT_RPATH='$ORIGIN'

while IFS= read -r -d '' so; do
  base=$(basename "$so")
  if [[ "$base" == *metatomic_engine* ]]; then
    echo "patchelf engine $(basename "$so")"
    patchelf --set-rpath "$ENG_RPATH" "$so"
  elif [[ "$base" == _core* ]]; then
    echo "patchelf core $(basename "$so")"
    patchelf --set-rpath "$CORE_RPATH" "$so"
  elif [[ "$so" == *".rgpot.mesonpy.libs"* ]]; then
    patchelf --set-rpath "$POT_RPATH" "$so" 2>/dev/null || true
  fi
done < <(find "$WORK" -type f \( -name '*.so' -o -name '*.so.*' \) -print0)

bad=0
while IFS= read -r -d '' so; do
  rp=$(readelf -d "$so" 2>/dev/null | sed -n 's/.*Library \(runpath\|rpath\): \[\(.*\)\]/\2/p' || true)
  if echo "$rp" | grep -E '/home/|/Users/|/bbdir|/\.mesonpy' >/dev/null 2>&1; then
    echo "ERROR: absolute host path in $(basename "$so"): $rp" >&2
    bad=1
  fi
done < <(find "$WORK" -type f -name '*.so' -print0)
[[ $bad -eq 0 ]] || exit 1

python3 - <<PY
import zipfile
from pathlib import Path
root = Path("$WORK")
out = Path("$WHL")
tmp = out.with_suffix(".whl.tmp")
with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_DEFLATED) as z:
    for p in sorted(root.rglob("*")):
        if p.is_file():
            z.write(p, p.relative_to(root).as_posix())
tmp.replace(out)
print("REPAIR_OK", out)
PY
