#!/usr/bin/env bash
# Orchestrate pure C++ Psi4 backend: libpsi4-cpp + meson -Dwith_psi4=true
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP_PREFIX="${LIBPSI4_PREFIX:-$ROOT/third_party/libpsi4-cpp/install}"
BUILDDIR="${RGPOT_PSI4_BUILDDIR:-$ROOT/build-psi4}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

echo "==> [1/3] Building third_party/libpsi4-cpp -> $TP_PREFIX"
bash "$ROOT/third_party/libpsi4-cpp/build.sh"

INC="$TP_PREFIX/include"
if [[ ! -d "$INC/psi4" ]]; then
  # Fallback: use in-tree headers if install step skipped headers
  INC="$ROOT/ref_psi4/psi4/include"
fi

echo "==> [2/3] Meson setup ($BUILDDIR) with -Dwith_psi4=true"
if [[ ! -d "$BUILDDIR" ]]; then
  meson setup "$BUILDDIR" "$ROOT" \
    -Dwith_psi4=true \
    -Dwith_tests=true \
    -Dpsi4_includedir="$INC" \
    "$@"
else
  meson setup --reconfigure "$BUILDDIR" "$ROOT" \
    -Dwith_psi4=true \
    -Dwith_tests=true \
    -Dpsi4_includedir="$INC" \
    "$@" 2>/dev/null || \
  meson configure "$BUILDDIR" \
    -Dwith_psi4=true \
    -Dwith_tests=true \
    -Dpsi4_includedir="$INC"
fi

echo "==> [3/3] Compile"
meson compile -C "$BUILDDIR" -j "$JOBS"

LIBDIR="$TP_PREFIX/lib"
ENGINE=""
for cand in \
  "$BUILDDIR/CppCore/rgpot/Psi4Pot/libpsi4_engine.so" \
  "$BUILDDIR/CppCore/rgpot/Psi4Pot/libpsi4_engine.dylib" \
  "$BUILDDIR/libpsi4_engine.so"; do
  if [[ -f "$cand" ]]; then ENGINE="$cand"; break; fi
done

PSI4SO=""
for cand in "$LIBDIR/libpsi4.so" "$LIBDIR/libpsi4.dylib"; do
  if [[ -f "$cand" ]]; then PSI4SO="$cand"; break; fi
done

DATADIR="$ROOT/ref_psi4/psi4/share/psi4"

cat <<EOF

Build complete.

Runtime environment (pure C++, no Python):
  export LD_LIBRARY_PATH="$LIBDIR:\${LD_LIBRARY_PATH:-}"
  export DYLD_LIBRARY_PATH="$LIBDIR:\${DYLD_LIBRARY_PATH:-}"   # macOS
  export RGPOT_PSI4_SO="${PSI4SO:-$LIBDIR/libpsi4.so}"
  export RGPOT_PSI4_ENGINE="${ENGINE:-<builddir>/.../libpsi4_engine.so}"
  export PSIDATADIR="$DATADIR"

Run tests (skips if engine/libs missing):
  meson test -C "$BUILDDIR" Psi4test --print-errorlogs

EOF
