#!/usr/bin/env bash
# Fail unless librgpot's Lepton symbols match the with_expr gate.
#
#   check_lepton_symbols.sh <librgpot> present
#   check_lepton_symbols.sh <librgpot> absent
set -euo pipefail

lib="${1:-}"
expect="${2:-}"
if [[ -z "$lib" || -z "$expect" ]]; then
  echo "usage: $0 <path to librgpot> present|absent" >&2
  exit 2
fi
if [[ ! -f "$lib" ]]; then
  echo "lepton-symbols: $lib not found" >&2
  exit 2
fi
if [[ "$expect" != "present" && "$expect" != "absent" ]]; then
  echo "lepton-symbols: expect present or absent, got $expect" >&2
  exit 2
fi

dump_syms() {
  if command -v nm >/dev/null 2>&1; then
    nm -C --defined-only "$lib" || true
    return
  fi
  if command -v readelf >/dev/null 2>&1; then
    readelf -Ws "$lib" || true
    return
  fi
  echo "lepton-symbols: nm and readelf unavailable" >&2
  exit 2
}

hits=$(dump_syms | grep -E 'Lepton::|lepton::' || true)

if [[ "$expect" == "present" ]]; then
  if [[ -z "$hits" ]]; then
    echo "lepton-symbols: expected Lepton symbols in $lib with -Dwith_expr=true" >&2
    exit 1
  fi
  exit 0
fi

if [[ -n "$hits" ]]; then
  echo "lepton-symbols: unexpected Lepton symbols in $lib without -Dwith_expr:" >&2
  printf '%s\n' "$hits" >&2
  exit 1
fi
