#!/usr/bin/env python3
"""Merge rgpot linked/dlopen and eOn linked-ship xTB timings into one report.

Inputs are JSON files produced by:
  - rgpot CppCore/xtb_backend_bench --json ...
  - eOn client/xtb_ship_bench --json ...

Usage::

  python scripts/compare_xtb_backends.py \\
    --rgpot /tmp/rgpot.json --eon /tmp/eon.json \\
    --out /tmp/xtb_backend_bench.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rgpot", type=Path, required=True, help="rgpot bench JSON")
    ap.add_argument("--eon", type=Path, required=True, help="eOn ship bench JSON")
    ap.add_argument("--out", type=Path, required=True, help="merged report JSON")
    ap.add_argument(
        "--tol",
        type=float,
        default=0.05,
        help="relative band for as-fast-or-faster (default 0.05)",
    )
    args = ap.parse_args()

    rg = json.loads(args.rgpot.read_text())
    eo = json.loads(args.eon.read_text())

    dl = float(rg["rgpot_dlopen_mean_ms"])
    link = float(rg["rgpot_linked_mean_ms"])
    eon = float(eo["eon_linked_mean_ms"])

    ratio_vs_eon = dl / eon if eon > 0 else float("inf")
    as_fast = dl <= eon * (1.0 + args.tol)

    report = {
        "system": rg.get("system", "water"),
        "method": rg.get("method", "GFN2xTB"),
        "n_atoms": rg.get("n_atoms", 3),
        "warmup": rg.get("warmup"),
        "iters": rg.get("iters"),
        "rgpot_linked_mean_ms": link,
        "rgpot_dlopen_mean_ms": dl,
        "eon_linked_ship_mean_ms": eon,
        "ratio_dlopen_over_eon_ship": ratio_vs_eon,
        "ratio_dlopen_over_rgpot_linked": rg.get("ratio_dlopen_over_linked"),
        "dlopen_as_fast_or_faster_than_eon_ship": as_fast,
        "tol_relative": args.tol,
        "energy_rgpot_linked_eV": rg.get("energy_linked_eV"),
        "energy_rgpot_dlopen_eV": rg.get("energy_dlopen_eV"),
        "energy_eon_ship_eV": eo.get("energy_eV"),
        "conclusion": (
            "dlopen as-fast-or-faster than eOn linked ship"
            if as_fast
            else f"dlopen slower than eOn ship by factor {ratio_vs_eon:.3f}"
        ),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    print("conclusion:", report["conclusion"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
