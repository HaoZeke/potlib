#!/usr/bin/env bash
# Inject staged multi-ABI engines into a wheel:
#   STAGING/torch-X.Y/libmetatomic_engine.so -> rgpot/lib/torch-X.Y/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WHL="${1:?wheel}"
STAGING="${2:-$ROOT/build/multi-abi-engines}"
command -v patchelf >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
python3 -m zipfile -e "$WHL" "$WORK"
n=0
for eng in "$STAGING"/torch-*/libmetatomic_engine.so; do
  [[ -f "$eng" ]] || continue
  maj=$(basename "$(dirname "$eng")")  # torch-X.Y
  dest="$WORK/rgpot/lib/$maj"
  mkdir -p "$dest"
  cp -a "$eng" "$dest/libmetatomic_engine.so"
  # ensure rpath (already set by build script, re-apply for safety)
  peer='$ORIGIN/../../../'
  ver=${maj#torch-}
  rpath="\$ORIGIN:${peer}torch/lib:${peer}metatensor/lib:${peer}vesin/lib"
  rpath+=":${peer}metatensor_torch/torch-${ver}/lib"
  rpath+=":${peer}metatomic/torch/torch-${ver}/lib"
  rpath+=":${peer}metatensor/torch/torch-${ver}/lib"
  patchelf --set-rpath "$rpath" "$dest/libmetatomic_engine.so"
  chmod a+rx "$dest/libmetatomic_engine.so"
  echo "inject $maj"
  n=$((n+1))
done
[[ $n -ge 1 ]] || { echo "ERROR: no engines in $STAGING" >&2; exit 1; }
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
    if p.is_file():
      z.write(p, p.relative_to(root).as_posix())
tmp.replace(out)
print("INJECT_OK", out, "engines", $n)
PY
bash "$ROOT/scripts/rgpot_repair_wheel.sh" "$WHL"
