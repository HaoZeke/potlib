#!/usr/bin/env bash
# gcov/lcov for fortcuh2 (CuH2 EAM Fortran library).
# Prefer capturing from a meson coverage build if present; else standalone.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="${1:-fortran_lcov.info}"
BUILD="${RGPOT_COV_BUILDDIR:-bbdir-cov}"

if ! command -v lcov >/dev/null 2>&1; then
  echo "ERROR: lcov not on PATH" >&2
  exit 1
fi

if [[ -d "$BUILD" ]]; then
  echo "==> extract fortcuh2 from meson coverage tree $BUILD"
  RAW=$(mktemp)
  lcov --directory "$BUILD" --capture --output-file "$RAW" 2>/dev/null \
    || lcov --directory "$BUILD" --capture --output-file "$RAW"
  lcov --extract "$RAW" '*/fortcuh2/*' '*/subprojects/fortcuh2/*' \
    --output-file "$OUT" 2>/dev/null || cp "$RAW" "$OUT"
  rm -f "$RAW"
else
  echo "==> standalone fortcuh2 instrumented build"
  if ! command -v gfortran >/dev/null 2>&1; then
    echo "ERROR: gfortran required when no meson coverage tree" >&2
    exit 1
  fi
  STG=$(mktemp -d)
  trap 'rm -rf "$STG"' EXIT
  SRC="$ROOT/subprojects/fortcuh2"
  # shellcheck disable=SC2086
  gfortran -c -O0 -g --coverage -cpp \
    "$SRC"/eam_dat.f90 "$SRC"/eam_isoc.f90 "$SRC"/eamroutines.f90 "$SRC"/main.f90 \
    -J"$STG" -o "$STG"/main.o 2>/dev/null || true
  # library objects only (skip main if it fails as program)
  gfortran -c -O0 -g --coverage -cpp \
    -J"$STG" -I"$STG" \
    "$SRC"/eam_dat.f90 -o "$STG"/eam_dat.o
  gfortran -c -O0 -g --coverage -cpp \
    -J"$STG" -I"$STG" \
    "$SRC"/eam_isoc.f90 -o "$STG"/eam_isoc.o
  gfortran -c -O0 -g --coverage -cpp \
    -J"$STG" -I"$STG" \
    "$SRC"/eamroutines.f90 -o "$STG"/eamroutines.o
  # Drive a trivial call if main exists as program
  if [[ -f "$SRC/main.f90" ]]; then
    gfortran -O0 -g --coverage -cpp \
      -J"$STG" -I"$STG" \
      "$SRC"/eam_dat.f90 "$SRC"/eam_isoc.f90 "$SRC"/eamroutines.f90 "$SRC"/main.f90 \
      -o "$STG"/fortcuh2_main || true
    if [[ -x "$STG/fortcuh2_main" ]]; then
      (cd "$STG" && ./fortcuh2_main) || true
    fi
  fi
  lcov --directory "$STG" --capture --output-file "$OUT" || true
  # Rewrite SF paths to repo paths
  python3 - "$OUT" "$SRC" <<'PY'
import os, sys
outp, src = sys.argv[1], sys.argv[2]
if not os.path.isfile(outp) or os.path.getsize(outp) == 0:
    # synthesize empty-but-valid? fail
    raise SystemExit("standalone fortcuh2 produced no LCOV")
lines = open(outp, encoding="utf-8", errors="replace").read().splitlines(True)
fixed = []
for line in lines:
    if line.startswith("SF:"):
        p = line[3:].strip()
        base = os.path.basename(p)
        cand = os.path.join(src, base)
        if os.path.isfile(cand):
            fixed.append(f"SF:{os.path.abspath(cand)}\n")
        else:
            fixed.append(line)
    else:
        fixed.append(line)
open(outp, "w", encoding="utf-8").writelines(fixed)
PY
fi

test -s "$OUT"
python3 - "$OUT" <<'PY'
import sys
hits = tot = 0
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if line.startswith("DA:"):
        h = int(line.split(":")[1].split(",")[1])
        tot += 1
        hits += h > 0
print(f"fortran lcov {100*hits/tot:.1f}% {hits}/{tot}" if tot else "empty")
if tot == 0:
    raise SystemExit(1)
PY
echo "OK wrote $OUT"
