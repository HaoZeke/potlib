#!/usr/bin/env bash
# Structural gate: multi-flag Codecov for rust/cpp/python/fortran.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COV_YML="$ROOT/codecov.yml"
WF="$ROOT/.github/workflows/coverage.yml"
fail=0
die() { echo "ERROR: $*" >&2; fail=1; }
ok() { echo "OK: $*"; }

[[ -f "$COV_YML" ]] || die "missing $COV_YML"
[[ -f "$WF" ]] || die "missing $WF"

for flag in rust cpp python fortran; do
  grep -qE "name:[[:space:]]*${flag}" "$COV_YML" && ok "codecov.yml flag $flag" || die "missing flag $flag"
done
grep -q 'carryforward: true' "$COV_YML" || die "missing carryforward"
grep -q 'informational: true' "$COV_YML" || die "missing informational"

for flag in rust cpp python fortran; do
  if grep -E "flags:[[:space:]]*${flag}" "$WF" | grep -vq '^\s*#'; then
    ok "coverage.yml flags: $flag"
  else
    die "coverage.yml missing flags: $flag"
  fi
done
grep -q 'use_oidc: true' "$WF" || die "missing use_oidc"
grep -q 'id-token: write' "$WF" || die "missing id-token: write"
grep -q 'fail_ci_if_error: false' "$WF" || die "missing soft-fail"
grep -q 'app.codecov.io' "$WF" || die "missing dashboard note"

for s in run_coverage_rust.sh run_coverage_cpp.sh run_coverage_python.sh run_coverage_fortran.sh; do
  [[ -x "$ROOT/scripts/$s" ]] || die "missing $s"
  ok "scripts/$s"
done

[[ "$fail" -eq 0 ]] || { echo "check_codecov_config: FAILED" >&2; exit 1; }
echo "check_codecov_config: all checks passed"
