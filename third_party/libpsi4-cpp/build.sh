#!/usr/bin/env bash
# Build pure C++ libpsi4 from ref_psi4 sources into install/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TP="$ROOT/third_party/libpsi4-cpp"
BUILD="${LIBPSI4_BUILD_DIR:-$TP/build}"
PREFIX="${LIBPSI4_PREFIX:-$TP/install}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

if [[ ! -d "$ROOT/ref_psi4/psi4/src/psi4" ]]; then
  if [[ -f "$ROOT/ref_psi4.zip" ]]; then
    echo "Extracting ref_psi4.zip ..."
    (cd "$ROOT" && unzip -q -o ref_psi4.zip)
  else
    echo "ERROR: ref_psi4/psi4/src/psi4 not found and no ref_psi4.zip" >&2
    exit 1
  fi
fi

mkdir -p "$BUILD"
cmake -S "$TP" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  "$@"

cmake --build "$BUILD" -j"$JOBS"
cmake --install "$BUILD"

echo "Installed libpsi4 to: $PREFIX"
echo "  lib:     $PREFIX/lib/libpsi4.so (or .dylib)"
echo "  headers: $PREFIX/include/psi4"
echo "Set PSIDATADIR to a psi4 share tree, e.g.:"
echo "  export PSIDATADIR=$ROOT/ref_psi4/psi4/share/psi4"
