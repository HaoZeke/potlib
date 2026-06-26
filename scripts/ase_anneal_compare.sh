#!/usr/bin/env bash
# Frictionless ASE / scipy / anneal demonstration on rgpot potentials.
# One command (run in the rpctest pixi env):
#
#     pixi run -e rpctest ase-anneal-compare
#
# It builds potserv if needed, ensures the `anneal` and `readcon` Python modules
# are importable (building them from sibling checkouts when absent), then runs:
#   1. tests/ase_vs_anneal.py  -- ASE LBFGS vs scipy dual_annealing vs anneal
#      portfolio on an LJ7 cluster (global minimization showcase).
#   2. tests/con_relax_demo.py -- readcon -> rgpot -> ASE relaxation of a CuH2
#      structure loaded from a .con file.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${RGPOT_BUILD_DIR:-$ROOT/build-rpc}"
ANNEAL_SRC="${ANNEAL_SRC:-$ROOT/../../Rust/anneal}"
READCON_SRC="${READCON_SRC:-$ROOT/../../Rust/readcon-core}"

# Build a maturin Python module from a sibling Rust crate when it is not already
# importable. The conda cc rejects the global cargo config's mold linker flag,
# so prefer the host clang/cargo for these wheels.
ensure_module() {
    local module="$1" src="$2"
    if python -c "import ${module}" 2>/dev/null; then return 0; fi
    if [ ! -d "$src" ]; then
        echo "[ase-anneal] ${module} not importable and no source at ${src}" >&2
        echo "             set $(echo "${module}" | tr a-z A-Z)_SRC or pip install ${module}." >&2
        exit 1
    fi
    echo "[ase-anneal] building the ${module} Python module from ${src} ..."
    local wh; wh="$(mktemp -d)"
    PATH="/usr/bin:$HOME/.cargo/bin:$PATH" CC="${HOST_CC:-cc}" CXX="${HOST_CXX:-c++}" \
        maturin build --release -m "${src}/Cargo.toml" -i "$(command -v python)" -o "$wh"
    python -m pip install --force-reinstall --no-deps "$wh"/${module}-*.whl
}

# 1. Build the RPC potential server once.
if [ ! -x "$BUILD/CppCore/potserv" ]; then
    echo "[ase-anneal] building potserv (-Dwith_rpc=true) ..."
    meson setup "$BUILD" -Dwith_rpc=true >/dev/null
    meson compile -C "$BUILD" potserv
fi
export RGPOT_POTSERV_BIN="$BUILD/CppCore/potserv"

# 2. Ensure the Python modules used by the demos.
ensure_module anneal "$ANNEAL_SRC"
ensure_module readcon "$READCON_SRC"

# 3. Run the demonstrations.
echo
python "$ROOT/tests/ase_vs_anneal.py"
echo
python "$ROOT/tests/con_relax_demo.py"
