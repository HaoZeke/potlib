#!/usr/bin/env bash
# Rewrite absolute purelib RUNPATHs to $ORIGIN-relative site-packages paths
# so the Metatomic engine resolves torch/metatomic next to the install.
set -euo pipefail
WHL="${1:?wheel path}"
command -v patchelf >/dev/null
command -v python3 >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
python3 -m zipfile -e "$WHL" "$WORK"
# Discover torch major from any absolute runpath or default
TORCH_MAJ=$(readelf -d "$WORK"/.rgpot.mesonpy.libs/libmetatomic_engine.so 2>/dev/null \
  | sed -n 's/.*torch-\([0-9]\+\.[0-9]\+\).*/\1/p' | head -1 || true)
TORCH_MAJ=${TORCH_MAJ:-2.9}
# Relative from .rgpot.mesonpy.libs/ to site-packages peers
ENG_RPATH="\$ORIGIN:\$ORIGIN/../torch/lib:\$ORIGIN/../metatensor/lib:\$ORIGIN/../metatensor_torch/torch-${TORCH_MAJ}/lib:\$ORIGIN/../metatomic/torch/torch-${TORCH_MAJ}/lib:\$ORIGIN/../vesin/lib"
CORE_RPATH="\$ORIGIN:\$ORIGIN/../.rgpot.mesonpy.libs"

while IFS= read -r -d '' so; do
  base=$(basename "$so")
  if [[ "$base" == *metatomic_engine* ]]; then
    echo "patchelf engine $so -> $ENG_RPATH"
    patchelf --set-rpath "$ENG_RPATH" "$so"
  elif [[ "$base" == _core* ]]; then
    echo "patchelf core $so -> $CORE_RPATH"
    patchelf --set-rpath "$CORE_RPATH" "$so"
  elif [[ "$so" == *".rgpot.mesonpy.libs"* ]]; then
    # pot libs: same-dir only
    patchelf --set-rpath "\$ORIGIN" "$so" 2>/dev/null || true
  fi
done < <(find "$WORK" -type f \( -name '*.so' -o -name '*.so.*' \) -print0)

# Verify no /home absolute paths remain in rpath
while IFS= read -r -d '' so; do
  rp=$(readelf -d "$so" 2>/dev/null | sed -n 's/.*Library \(runpath\|rpath\): \[\(.*\)\]/\2/p' || true)
  if echo "$rp" | grep -E '/home/|/bbdir|/\.mesonpy' >/dev/null 2>&1; then
    echo "ERROR: absolute host path remains in $so: $rp" >&2
    exit 1
  fi
done < <(find "$WORK" -type f -name '*.so' -print0)

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
