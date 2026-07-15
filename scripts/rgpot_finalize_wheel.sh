#!/usr/bin/env bash
# Finalize rgpot wheel for PyPI:
#  1) repair RPATH on all .so
#  2) place a *clean* engine under rgpot/lib/torch-X.Y/ (copy of primary, not auditwheel-mangled)
#  3) optional: auditwheel manylinux with engine excluded from rewrite (caller does that)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WHL="${1:?wheel}"
MAJ="${RGPOT_TORCH_MAJOR:-}"
bash "$ROOT/scripts/rgpot_repair_wheel.sh" "$WHL"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
python3 -m zipfile -e "$WHL" "$WORK"
PRIMARY=$(find "$WORK" -path '*mesonpy.libs/libmetatomic_engine.so' -print -quit)
if [[ -z "$PRIMARY" ]]; then
  PRIMARY=$(find "$WORK" -name 'libmetatomic_engine.so' -print -quit)
fi
if [[ -n "$PRIMARY" ]]; then
  if [[ -z "$MAJ" ]]; then
    MAJ=$(readelf -d "$PRIMARY" 2>/dev/null | sed -n 's/.*torch-\([0-9]\+\.[0-9]\+\).*/\1/p' | head -1 || true)
  fi
  MAJ=${MAJ:-2.9}
  dest="$WORK/rgpot/lib/torch-$MAJ"
  mkdir -p "$dest"
  # Clean copy of primary (same bytes); set multi-ABI RPATH
  cp -a "$PRIMARY" "$dest/libmetatomic_engine.so"
  peer='$ORIGIN/../../../'
  rpath="\$ORIGIN:${peer}torch/lib:${peer}metatensor/lib:${peer}vesin/lib"
  rpath+=":${peer}metatensor_torch/torch-${MAJ}/lib"
  rpath+=":${peer}metatomic/torch/torch-${MAJ}/lib"
  rpath+=":${peer}metatensor/torch/torch-${MAJ}/lib"
  patchelf --set-rpath "$rpath" "$dest/libmetatomic_engine.so"
  chmod a+rx "$dest/libmetatomic_engine.so" "$PRIMARY" 2>/dev/null || true
  echo "MULTI_ABI_ENGINE torch-$MAJ"
fi
python3 - <<PY
import zipfile, hashlib, base64
from pathlib import Path
root=Path("$WORK"); out=Path("$WHL")
rec=list(root.glob("*.dist-info/RECORD"))[0]
lines=[]
for p in sorted(root.rglob("*")):
  if p.is_file() and p.name!="RECORD":
    rel=p.relative_to(root).as_posix()
    data=p.read_bytes()
    h=base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    lines.append(f"{rel},sha256={h},{len(data)}")
lines.append(f"{rec.relative_to(root).as_posix()},,")
rec.write_text("\\n".join(lines)+"\\n")
tmp=out.with_suffix(".whl.tmp")
with zipfile.ZipFile(tmp,"w",compression=zipfile.ZIP_DEFLATED) as z:
  for p in sorted(root.rglob("*")):
    if p.is_file(): z.write(p, p.relative_to(root).as_posix())
tmp.replace(out)
print("FINALIZE_OK", out)
PY
bash "$ROOT/scripts/rgpot_repair_wheel.sh" "$WHL"
