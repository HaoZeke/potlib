#!/usr/bin/env bash
# LCOV for rgpot-core (C ABI + optional RPC feature).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT_LCOV="${1:-lcov.info}"
OUT_JSON="${2:-rust_codecov.json}"
IGNORE='(/tests/|/benches/|/potctl/)'

unset RUSTC_WRAPPER SCCACHE_GHA_ENABLED || true
export RUSTC_WRAPPER=""
export CARGO_INCREMENTAL=0

if [[ -z "${CC:-}" ]] && command -v clang >/dev/null 2>&1; then
  export CC=clang CXX="${CXX:-clang++}"
fi

echo "==> cargo llvm-cov -p rgpot-core --all-features"
cargo llvm-cov -p rgpot-core --all-features --no-fail-fast \
  --ignore-filename-regex="${IGNORE}" \
  --lcov --output-path "${OUT_LCOV}"

cargo llvm-cov report --codecov --output-path "${OUT_JSON}" \
  --ignore-filename-regex="${IGNORE}"

test -s "$OUT_LCOV"
test -s "$OUT_JSON"
python3 - "$OUT_LCOV" <<'PY'
import sys
hits = tot = 0
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if line.startswith("DA:"):
        h = int(line.split(":")[1].split(",")[1])
        tot += 1
        hits += h > 0
print(f"rust lcov {100*hits/tot:.2f}% {hits}/{tot}" if tot else "empty")
if tot == 0:
    raise SystemExit(1)
PY
echo "OK wrote $OUT_LCOV and $OUT_JSON"
