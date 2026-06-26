#!/usr/bin/env bash
# Render the rgpot LJ double-well 2D PES (local optimizer vs anneal global).
# Builds potserv if needed and ensures the anneal Python module, then plots.
#
#     pixi run -e rpctest pes-double-well
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${RGPOT_BUILD_DIR:-$ROOT/build-rpc}"
ANNEAL_SRC="${ANNEAL_SRC:-$ROOT/../../Rust/anneal}"

if [ ! -x "$BUILD/CppCore/potserv" ]; then
    meson setup "$BUILD" -Dwith_rpc=true >/dev/null
    meson compile -C "$BUILD" potserv
fi
export RGPOT_POTSERV_BIN="$BUILD/CppCore/potserv"

if ! python -c "import anneal" 2>/dev/null; then
    [ -d "$ANNEAL_SRC" ] || { echo "anneal not importable; set ANNEAL_SRC" >&2; exit 1; }
    wh="$(mktemp -d)"
    PATH="/usr/bin:$HOME/.cargo/bin:$PATH" CC="${HOST_CC:-cc}" CXX="${HOST_CXX:-c++}" \
        maturin build --release --features python -m "$ANNEAL_SRC/Cargo.toml" \
        -i "$(command -v python)" -o "$wh"
    python -m pip install --force-reinstall --no-deps "$wh"/anneal-*.whl
fi

exec python "$ROOT/tests/pes_double_well.py"
