#!/usr/bin/env bash
# Build rgpot (+optional eOn) xTB benches on this host and run compare twice.
# Requires pixi env with xtb (e.g. rgpot xtbbld) and PKG_CONFIG_PATH for xtb.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${OUT_DIR:-}"
EON_ROOT="${EON_ROOT:-}"
WARMUP="${WARMUP:-5}"
ITERS="${ITERS:-50}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out-dir) OUT_DIR="$2"; shift 2 ;;
    --eon-root) EON_ROOT="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --iters) ITERS="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$OUT_DIR" ]]; then
  echo "usage: $0 --out-dir DIR [--eon-root PATH]" >&2
  exit 2
fi
mkdir -p "$OUT_DIR"

PIXI_ENV="${PIXI_ENV:-xtbbld}"
RGPOT_BB="${RGPOT_BB:-$ROOT/bbdir-xtb-bench}"
EON_BB="${EON_BB:-}"

echo "== rgpot xtb build =="
cd "$ROOT"
if command -v pixi >/dev/null 2>&1; then
  pixi run -e "$PIXI_ENV" meson setup "$RGPOT_BB" \
    -Dwith_xtb=true -Dwith_tests=true -Dwith_rpc=false -Dwith_cache=false \
    --buildtype=release --reconfigure 2>/dev/null \
    || pixi run -e "$PIXI_ENV" meson setup "$RGPOT_BB" \
      -Dwith_xtb=true -Dwith_tests=true -Dwith_rpc=false -Dwith_cache=false \
      --buildtype=release
  pixi run -e "$PIXI_ENV" meson compile -C "$RGPOT_BB"
  pixi run -e "$PIXI_ENV" meson test -C "$RGPOT_BB" --suite xtb --print-errorlogs \
    | tee "$OUT_DIR/rgpot_xtb_tests.log"
else
  meson setup "$RGPOT_BB" -Dwith_xtb=true -Dwith_tests=true -Dwith_rpc=false \
    -Dwith_cache=false --buildtype=release --reconfigure 2>/dev/null \
    || meson setup "$RGPOT_BB" -Dwith_xtb=true -Dwith_tests=true -Dwith_rpc=false \
      -Dwith_cache=false --buildtype=release
  meson compile -C "$RGPOT_BB"
  meson test -C "$RGPOT_BB" --suite xtb --print-errorlogs \
    | tee "$OUT_DIR/rgpot_xtb_tests.log"
fi

ENGINE="$(find "$RGPOT_BB" -name 'libxtb_engine.so' | head -1)"
BENCH="$(find "$RGPOT_BB" -name 'xtb_backend_bench' -type f | head -1)"
export RGPOT_XTB_ENGINE="$ENGINE"
echo "RGPOT_XTB_ENGINE=$RGPOT_XTB_ENGINE"

run_pair() {
  local tag=$1
  "$BENCH" --warmup "$WARMUP" --iters "$ITERS" \
    --json "$OUT_DIR/rgpot_${tag}.json" | tee "$OUT_DIR/rgpot_${tag}.log"
  if [[ -n "${EON_SHIP_BENCH:-}" && -x "${EON_SHIP_BENCH}" ]]; then
    "$EON_SHIP_BENCH" --warmup "$WARMUP" --iters "$ITERS" \
      --json "$OUT_DIR/eon_${tag}.json" | tee "$OUT_DIR/eon_${tag}.log"
  elif [[ -n "$EON_ROOT" ]]; then
    EON_BB="${EON_BB:-$EON_ROOT/bbdir-xtb-ship}"
    if [[ ! -x "$EON_BB/client/xtb_ship_bench" ]]; then
      echo "== eOn xtb ship build =="
      cd "$EON_ROOT"
      if command -v pixi >/dev/null 2>&1; then
        pixi run -e ci-xtb meson setup "$EON_BB" -Dwith_xtb=true -Dwith_tests=true \
          -Dwith_rgpot=false --buildtype=release --reconfigure 2>/dev/null \
          || pixi run -e ci-xtb meson setup "$EON_BB" -Dwith_xtb=true -Dwith_tests=true \
            -Dwith_rgpot=false --buildtype=release
        pixi run -e ci-xtb meson compile -C "$EON_BB" xtb_ship_bench
      else
        meson setup "$EON_BB" -Dwith_xtb=true -Dwith_tests=true -Dwith_rgpot=false \
          --buildtype=release --reconfigure 2>/dev/null \
          || meson setup "$EON_BB" -Dwith_xtb=true -Dwith_tests=true -Dwith_rgpot=false \
            --buildtype=release
        meson compile -C "$EON_BB" xtb_ship_bench
      fi
    fi
    "$EON_BB/client/xtb_ship_bench" --warmup "$WARMUP" --iters "$ITERS" \
      --json "$OUT_DIR/eon_${tag}.json" | tee "$OUT_DIR/eon_${tag}.log"
  else
    echo "EON_ROOT not set; writing stub eOn times from rgpot linked as proxy" >&2
    python3 - <<PY
import json
from pathlib import Path
rg=json.loads(Path("$OUT_DIR/rgpot_${tag}.json").read_text())
# Fail closed: require real eOn ship for final report
raise SystemExit("set --eon-root to eOn checkout for ship baseline")
PY
  fi
  python3 "$ROOT/scripts/compare_xtb_backends.py" \
    --rgpot "$OUT_DIR/rgpot_${tag}.json" \
    --eon "$OUT_DIR/eon_${tag}.json" \
    --out "$OUT_DIR/xtb_backend_bench_${tag}.json"
}

run_pair run1
run_pair run2
cp -f "$OUT_DIR/xtb_backend_bench_run2.json" "$OUT_DIR/xtb_backend_bench.json"
echo "DONE reports under $OUT_DIR"
ls -la "$OUT_DIR"
