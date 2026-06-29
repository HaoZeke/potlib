#!/usr/bin/env bash
# Time shipped rgpot::CPMDPot forceImpl (BLYP) vs real cpmd.x subprocess on same host.
# Requires: timed binaries built by caller, CPMDC_LIBRARY (real libcpmdc), CPMDC_PSEUDO_DIR, CPMDX.
set -euo pipefail
SCRATCH=${SCRATCH:-$(pwd)}
NFORCE=${NFORCE:-1}
INPROC=${INPROC_BIN:-$SCRATCH/timed_cpmd_inprocess_blyp}
SUB=${SUBPROC_BIN:-$SCRATCH/timed_cpmd_subprocess_blyp.sh}
test -x "$INPROC"
test -x "$SUB"
test -n "${CPMDC_LIBRARY:-}"
test -n "${CPMDC_PSEUDO_DIR:-}"
case "$CPMDC_LIBRARY" in *fake_engine*) echo "refuse fake engine"; exit 2;; esac
"$INPROC" "$NFORCE" | tee "$SCRATCH/cpmd_inprocess_blyp_timing1.log"
"$INPROC" "$NFORCE" | tee "$SCRATCH/cpmd_inprocess_blyp_timing2.log"
"$SUB" "$NFORCE" | tee "$SCRATCH/cpmd_socket_blyp_timing1.log"
"$SUB" "$NFORCE" | tee "$SCRATCH/cpmd_socket_blyp_timing2.log"
python3 - <<'PY'
from pathlib import Path
import os
S = Path(os.environ.get("SCRATCH", "."))
def wall(p):
    for line in Path(p).read_text().splitlines():
        if line.startswith("wall_seconds="):
            return float(line.split("=",1)[1])
    raise SystemExit(f"missing wall_seconds in {p}")
ip = [wall(S/"cpmd_inprocess_blyp_timing1.log"), wall(S/"cpmd_inprocess_blyp_timing2.log")]
bl = [wall(S/"cpmd_socket_blyp_timing1.log"), wall(S/"cpmd_socket_blyp_timing2.log")]
ratio = max(ip) / min(bl)
print(f"ratio_worst_ip_over_best_baseline={ratio}")
print(f"same_speed_category={ratio <= 2.0}")
raise SystemExit(0 if ratio <= 2.0 else 1)
PY
