#!/usr/bin/env bash
# In-process NWChem embed only (stable C ABI -> libnwchem_engine.so). No CLI.
#
#   scripts/setup_nwchem_embed.sh clone      # shallow clone -> third_party/nwchem
#   scripts/setup_nwchem_embed.sh configure  # meson -Dwith_nwchem + nwchem_root
#   scripts/setup_nwchem_embed.sh calc       # direct NWChemPot water (needs built NWChem)
#
# You must build NWChem itself under third_party/nwchem (NWCHEM_TOP) once;
# conda/pixi `nwchem` packages are insufficient for embed (no src/include tree).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

NWCHEM_SRC="${NWCHEM_SRC:-$ROOT/third_party/nwchem}"
NWCHEM_TARGET="${NWCHEM_TARGET:-LINUX64}"
BUILD_DIR="${RGPOT_NWCHEM_BUILD:-bbdir_nwchem_embed}"
ENGINE="${BUILD_DIR}/CppCore/rgpot/NWChemPot/libnwchem_engine.so"
SMOKE_BIN="${BUILD_DIR}/nwchem_calc_smoke"

die() { echo "error: $*" >&2; exit 1; }

cmd_clone() {
  mkdir -p third_party
  if [[ -d "$NWCHEM_SRC/.git" ]]; then
    echo "already cloned: $NWCHEM_SRC"
  else
    git clone --depth 1 --branch master \
      https://github.com/nwchemgit/nwchem.git "$NWCHEM_SRC"
  fi
  echo "NWCHEM_TOP=$NWCHEM_SRC"
  echo "Build NWChem here before configure (see NWChem INSTALL). Need:"
  echo "  $NWCHEM_SRC/src/include/rtdb.fh"
  echo "  $NWCHEM_SRC/lib/$NWCHEM_TARGET/libnwcutil.a (or full task libs)"
}

cmd_configure() {
  [[ -d "$NWCHEM_SRC/src/include" ]] || die "missing $NWCHEM_SRC/src/include — run clone and build NWChem"
  if [[ ! -d "$BUILD_DIR" ]]; then
    meson setup "$BUILD_DIR" \
      -Dwith_tests=false -Dwith_rpc=false \
      -Dwith_nwchem=true \
      -Dnwchem_root="$NWCHEM_SRC" \
      -Dnwchem_target="$NWCHEM_TARGET"
  else
    meson setup --reconfigure "$BUILD_DIR" \
      -Dwith_tests=false -Dwith_rpc=false \
      -Dwith_nwchem=true \
      -Dnwchem_root="$NWCHEM_SRC" \
      -Dnwchem_target="$NWCHEM_TARGET"
  fi
  meson compile -C "$BUILD_DIR"
  [[ -f "$ENGINE" ]] || die "engine not produced: $ENGINE (embed build failed)"

  CXX="${CXX:-c++}"
  "$CXX" -std=c++17 -O2 \
    -ICppCore -ICppCore/rgpot/NWChemPot \
    scripts/nwchem_calc_smoke.cc \
    "${BUILD_DIR}/CppCore/rgpot/NWChemPot/libnwchempot.a" \
    "${BUILD_DIR}/CppCore/librgpot_core.a" \
    -ldl -o "$SMOKE_BIN"
  echo "built $ENGINE"
  echo "built $SMOKE_BIN (direct NWChemPot, no RPC)"
}

cmd_calc() {
  [[ -f "$ENGINE" ]] || die "run configure first (missing $ENGINE)"
  [[ -x "$SMOKE_BIN" ]] || die "run configure first (missing $SMOKE_BIN)"
  export RGPOT_NWCHEM_ENGINE="$(cd "$(dirname "$ENGINE")" && pwd)/$(basename "$ENGINE")"
  export NWCHEM_TOP="${NWCHEM_TOP:-$NWCHEM_SRC}"
  export LD_LIBRARY_PATH="${NWCHEM_TOP}/lib/${NWCHEM_TARGET}:${LD_LIBRARY_PATH:-}"
  echo "RGPOT_NWCHEM_ENGINE=$RGPOT_NWCHEM_ENGINE"
  echo "NWCHEM_TOP=$NWCHEM_TOP"
  exec "$SMOKE_BIN"
}

case "${1:-}" in
  clone) cmd_clone ;;
  configure|setup) cmd_configure ;;
  calc) cmd_calc ;;
  *)
    echo "usage: $0 clone|configure|calc" >&2
    exit 2
    ;;
esac
