#!/usr/bin/env bash
# Build libmetatomic_engine.so for every torch major metatomic ships.
# Stages: build/multi-abi-engines/torch-X.Y/libmetatomic_engine.so
#
# Links against isolated libtorch per major (CPU). When the build CPython has
# no torch wheel for an old major, downloads a manylinux wheel for cp312 and
# extracts libtorch (C++ ABI is per torch major, not per CPython tag).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PY="${RGPOT_BUILD_PYTHON:-python3}"
ABI_ROOT="${RGPOT_ABI_ROOT:-$HOME/tmp/rgpot-multi-abi}"
STAGING="${RGPOT_ABI_STAGING:-$ROOT/build/multi-abi-engines}"
mkdir -p "$ABI_ROOT" "$STAGING"

HOST_PURE=$("$PY" -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')
[[ -d "$HOST_PURE/metatomic/torch" ]] || { echo "ERROR: need metatomic-torch in $HOST_PURE" >&2; exit 1; }

if [[ -z "${RGPOT_TORCH_MAJORS:-}" ]]; then
  RGPOT_TORCH_MAJORS=$("$PY" - <<'PY'
from pathlib import Path
import sysconfig
p = Path(sysconfig.get_paths()["purelib"]) / "metatomic" / "torch"
print(" ".join(sorted(d.name.removeprefix("torch-") for d in p.glob("torch-*") if d.is_dir())))
PY
)
fi
echo "MAJORS=$RGPOT_TORCH_MAJORS"
echo "STAGING=$STAGING ABI_ROOT=$ABI_ROOT"

ensure_torch_prefix() {
  local maj="$1" prefix="$2"
  if [[ -f "$prefix/torch/lib/libtorch_cpu.so" && -f "$prefix/torch/share/cmake/Torch/TorchConfig.cmake" ]]; then
    return 0
  fi
  rm -rf "$prefix"
  mkdir -p "$prefix" "$ABI_ROOT/wheels-$maj"
  echo "  fetch torch $maj into $prefix"
  # 1) try install for this interpreter
  if "$PY" -m pip install -q "torch==${maj}.*" \
      --index-url https://download.pytorch.org/whl/cpu \
      --target "$prefix" 2>"$ABI_ROOT/pip-$maj.log"; then
    :
  elif "$PY" -m pip install -q "torch==${maj}.*" --target "$prefix" 2>>"$ABI_ROOT/pip-$maj.log"; then
    :
  else
    # 2) download a manylinux wheel for an older CPython tag and extract
    echo "  pip install failed; trying wheel download for cp312"
    if ! "$PY" -m pip download -q "torch==${maj}.*" \
        --index-url https://download.pytorch.org/whl/cpu \
        --only-binary=:all: \
        --python-version 312 --abi cp312 \
        --platform manylinux2014_x86_64 \
        --platform manylinux_2_17_x86_64 \
        --platform manylinux_2_28_x86_64 \
        -d "$ABI_ROOT/wheels-$maj" 2>>"$ABI_ROOT/pip-$maj.log"; then
      "$PY" -m pip download -q "torch==${maj}.*" \
        --only-binary=:all: \
        --python-version 312 --abi cp312 \
        --platform manylinux2014_x86_64 \
        -d "$ABI_ROOT/wheels-$maj" 2>>"$ABI_ROOT/pip-$maj.log" || true
    fi
    local whl
    whl=$(ls "$ABI_ROOT/wheels-$maj"/torch-*.whl 2>/dev/null | head -1 || true)
    if [[ -n "$whl" ]]; then
      echo "  extract $whl"
      "$PY" -m zipfile -e "$whl" "$prefix"
    fi
  fi
  # normalize version.py if missing
  if [[ -f "$prefix/torch/version.py" ]]; then
    :
  elif [[ -d "$prefix/torch" ]]; then
    echo "__version__ = '${maj}.0'" > "$prefix/torch/version.py"
  fi
  [[ -f "$prefix/torch/lib/libtorch_cpu.so" ]]
}

set_engine_rpath() {
  local so="$1" maj="$2"
  local peer='$ORIGIN/../../../'
  local rpath="\$ORIGIN:${peer}torch/lib:${peer}metatensor/lib:${peer}vesin/lib"
  rpath+=":${peer}metatensor_torch/torch-${maj}/lib"
  rpath+=":${peer}metatomic/torch/torch-${maj}/lib"
  rpath+=":${peer}metatensor/torch/torch-${maj}/lib"
  patchelf --set-rpath "$rpath" "$so"
  chmod a+rx "$so"
}

build_one() {
  local maj="$1"
  local prefix="$ABI_ROOT/prefix-$maj"
  local bdir="$ABI_ROOT/build-$maj"
  local outdir="$STAGING/torch-$maj"
  mkdir -p "$outdir"

  if [[ -f "$outdir/libmetatomic_engine.so" && "${RGPOT_FORCE_REBUILD:-0}" != "1" ]]; then
    echo "KEEP $outdir/libmetatomic_engine.so"
    return 0
  fi

  echo "=== BUILD engine torch-$maj ==="
  if ! ensure_torch_prefix "$maj" "$prefix"; then
    echo "SKIP torch-$maj: cannot obtain libtorch (log $ABI_ROOT/pip-$maj.log)" >&2
    return 0
  fi

  for pkg in metatensor metatensor_torch metatomic vesin; do
    [[ -d "$HOST_PURE/$pkg" ]] && ln -sfn "$HOST_PURE/$pkg" "$prefix/$pkg"
  done

  rm -rf "$bdir"
  (
    export PATH="$(dirname "$PY"):/usr/bin:$PATH"
    export PYTHONPATH="$prefix${PYTHONPATH:+:$PYTHONPATH}"
    export RGPOT_TORCH_ROOT="$prefix/torch"
    export CMAKE_PREFIX_PATH="$prefix/torch/share/cmake${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    "$PY" -c "from pathlib import Path; import os; r=Path(os.environ['RGPOT_TORCH_ROOT']); print('torch_root', r, 'cpu', (r/'lib'/'libtorch_cpu.so').is_file())"
    meson setup "$bdir" \
      -Dwith_python=false -Dwith_rpc=false -Dwith_tests=false -Dwith_examples=false \
      -Dwith_metatomic=true -Dwith_xtb=false -Dwith_tblite=false -Dwith_rust_core=false \
      -Dwith_cache=false -Dwith_eigen=false -Db_lto=false \
      >"$ABI_ROOT/meson-setup-$maj.log" 2>&1
    # confirm major in log / purelib probe
    if ! grep -q "$maj" "$ABI_ROOT/meson-setup-$maj.log" && ! grep -q "torch-$maj" "$bdir/meson-logs/meson-log.txt" 2>/dev/null; then
      echo "NOTE: meson-setup log for $maj (tail):" >&2
      tail -30 "$ABI_ROOT/meson-setup-$maj.log" >&2 || true
    fi
    meson compile -C "$bdir" metatomic_engine >"$ABI_ROOT/meson-compile-$maj.log" 2>&1
    SO=$(find "$bdir" -name 'libmetatomic_engine.so' | head -1)
    if [[ -z "$SO" ]]; then
      echo "FAIL compile torch-$maj" >&2
      tail -40 "$ABI_ROOT/meson-compile-$maj.log" >&2 || true
      return 1
    fi
    cp -a "$SO" "$outdir/libmetatomic_engine.so"
    set_engine_rpath "$outdir/libmetatomic_engine.so" "$maj"
    echo "OK torch-$maj ($(stat -c%s "$outdir/libmetatomic_engine.so") bytes)"
  )
}

ok=0; fail=0; skip=0
for maj in $RGPOT_TORCH_MAJORS; do
  if build_one "$maj"; then
    if [[ -f "$STAGING/torch-$maj/libmetatomic_engine.so" ]]; then ok=$((ok+1)); else skip=$((skip+1)); fi
  else
    fail=$((fail+1))
  fi
done
echo "=== staged ==="
find "$STAGING" -name 'libmetatomic_engine.so' | sort
echo "summary ok=$ok skip=$skip fail=$fail"
[[ $ok -ge 1 ]]
