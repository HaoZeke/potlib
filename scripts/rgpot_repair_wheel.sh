#!/usr/bin/env bash
# Rewrite absolute purelib RUNPATHs to $ORIGIN-relative site-packages paths.
#
# Engines live at either:
#   .rgpot.mesonpy.libs/libmetatomic_engine.so          (legacy single)
#   rgpot/lib/torch-X.Y/libmetatomic_engine.so          (multi-ABI)
# Each engine gets RUNPATH for *its* torch-X.Y only (never list all ABIs).
set -euo pipefail
WHL="${1:?wheel path}"
command -v patchelf >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
python3 -m zipfile -e "$WHL" "$WORK"

engine_rpath_for_maj() {
  local maj="$1"
  # From rgpot/lib/torch-X.Y/ → site-packages peers are ../../../
  # From .rgpot.mesonpy.libs/ → site-packages peers are ../
  local origin_to_site="$2"
  echo "\$ORIGIN:\${origin_to_site}torch/lib:\${origin_to_site}metatensor/lib:\${origin_to_site}vesin/lib:\${origin_to_site}metatensor_torch/torch-${maj}/lib:\${origin_to_site}metatomic/torch/torch-${maj}/lib:\${origin_to_site}metatensor/torch/torch-${maj}/lib"
}

CORE_RPATH='$ORIGIN:$ORIGIN/../.rgpot.mesonpy.libs:$ORIGIN/lib'
# librgpot lives under .rgpot.mesonpy.libs/. $ORIGIN alone cannot see
# site-packages/torch. RGPOT_TORCH_MAJOR adds the metatensor-torch ABI dir.
_maj="${RGPOT_TORCH_MAJOR:-}"
UMBRELLA_RPATH='$ORIGIN:$ORIGIN/../torch/lib:$ORIGIN/../metatensor/lib:$ORIGIN/../vesin/lib'
if [[ -n "$_maj" ]]; then
  UMBRELLA_RPATH+=":\$ORIGIN/../metatensor_torch/torch-${_maj}/lib"
  UMBRELLA_RPATH+=":\$ORIGIN/../metatomic/torch/torch-${_maj}/lib"
  UMBRELLA_RPATH+=":\$ORIGIN/../metatensor/torch/torch-${_maj}/lib"
fi

while IFS= read -r -d '' so; do
  base=$(basename "$so")
  rel=${so#"$WORK/"}
  if [[ "$base" == *metatomic_engine* ]]; then
    maj=""
    if [[ "$rel" =~ torch-([0-9]+\.[0-9]+)/ ]]; then
      maj="${BASH_REMATCH[1]}"
      # rgpot/lib/torch-X.Y/lib.so → three levels up to site-packages
      peer='$ORIGIN/../../../'
    else
      maj="${RGPOT_TORCH_MAJOR:-}"
      if [[ -z "$maj" ]]; then
        maj=$(readelf -d "$so" 2>/dev/null | sed -n 's/.*torch-\([0-9]\+\.[0-9]\+\).*/\1/p' | head -1 || true)
      fi
      maj=${maj:-2.9}
      peer='$ORIGIN/../'
    fi
    # Build rpath string without nested expansion bugs
    ENG_RPATH="\$ORIGIN:${peer}torch/lib:${peer}metatensor/lib:${peer}vesin/lib"
    ENG_RPATH+=":${peer}metatensor_torch/torch-${maj}/lib"
    ENG_RPATH+=":${peer}metatomic/torch/torch-${maj}/lib"
    ENG_RPATH+=":${peer}metatensor/torch/torch-${maj}/lib"
    echo "patchelf engine $rel (torch-$maj)"
    patchelf --set-rpath "$ENG_RPATH" "$so"
  elif [[ "$base" == _core* ]]; then
    echo "patchelf core $(basename "$so")"
    patchelf --set-rpath "$CORE_RPATH" "$so"
  elif [[ "$base" == librgpot.so* ]]; then
    echo "patchelf umbrella $rel"
    patchelf --set-rpath "$UMBRELLA_RPATH" "$so"
  elif [[ "$so" == *".rgpot.mesonpy.libs"* ]]; then
    patchelf --set-rpath '$ORIGIN' "$so" 2>/dev/null || true
  fi
done < <(find "$WORK" -type f \( -name '*.so' -o -name '*.so.*' \) -print0)

# zip/pip cannot store ELF soname symlinks. meson installs
# librgpot.so.3.0.1 (SONAME librgpot.so.3); _core NEEDs that name.
# Copy (not ln) so a second repair pass and pip extract both see real files.
LIBS=$(find "$WORK" -type d -name '.rgpot.mesonpy.libs' -print -quit)
if [[ -n "$LIBS" ]]; then
  real=$(find "$LIBS" -maxdepth 1 -type f -name 'librgpot.so.*.*' | head -1 || true)
  if [[ -n "$real" ]]; then
    cp -f "$real" "$LIBS/librgpot.so.3"
    cp -f "$real" "$LIBS/librgpot.so"
    echo "soname copies $(basename "$real") -> librgpot.so.3 + librgpot.so"
  fi
  # Vendor s-dftd3 / dftd4 and every NEEDED they pull from the micromamba
  # prefix (mctc-lib, toml-f, blas, …) next to librgpot.
  _dftd_root=/tmp/rgpot-dftd
  _copy_dftd() {
    local src="$1"
    local dest="$LIBS/$(basename "$src")"
    [[ -e "$dest" ]] && return 0
    cp -a "$src" "$dest"
    echo "vendor $(basename "$src") -> $(basename "$LIBS")"
    if command -v patchelf >/dev/null; then
      patchelf --set-rpath '$ORIGIN' "$dest" 2>/dev/null || true
    fi
  }
  if [[ -d "$_dftd_root/lib" || -d "$_dftd_root/lib64" ]]; then
    while IFS= read -r -d '' src; do
      _copy_dftd "$src"
    done < <(find "$_dftd_root/lib" "$_dftd_root/lib64" -maxdepth 1 \( -type f -o -type l \) \( \
      -name 'libs-dftd3.so*' -o -name 'libdftd4.so*' -o \
      -name 'libmctc-lib.so*' -o -name 'libtoml-f.so*' -o \
      -name 'libmulticharge.so*' \
    \) -print0 2>/dev/null)
    # Walk NEEDED of what we just copied and pull matching files from the prefix.
    _again=1
    while [[ "$_again" -eq 1 ]]; do
      _again=0
      for so in "$LIBS"/libs-dftd3.so* "$LIBS"/libdftd4.so* "$LIBS"/libmctc-lib.so* \
                "$LIBS"/libtoml-f.so* "$LIBS"/libmulticharge.so* "$LIBS"/libblas.so* \
                "$LIBS"/liblapack.so* "$LIBS"/libopenblas.so* "$LIBS"/libflexiblas.so*; do
        [[ -e "$so" ]] || continue
        command -v patchelf >/dev/null || continue
        while read -r need; do
          [[ -n "$need" ]] || continue
          [[ -e "$LIBS/$need" ]] && continue
          case "$need" in
            libc.so*|libm.so*|libpthread*|libdl.so*|librt.so*| \
            libgcc_s.so*|ld-linux*|libstdc++.so*|libgfortran.so*| \
            libquadmath.so*|libgomp.so*) continue ;;
          esac
          for d in "$_dftd_root/lib" "$_dftd_root/lib64"; do
            if [[ -e "$d/$need" ]]; then
              _copy_dftd "$d/$need"
              _again=1
            fi
          done
        done < <(patchelf --print-needed "$so" 2>/dev/null || true)
      done
    done
  fi
fi

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
