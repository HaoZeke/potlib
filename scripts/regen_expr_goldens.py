#!/usr/bin/env python3
"""Regenerate committed ExprPot algebra golden masters. rg.terra only.

Do not invoke from meson test. Do not invent looser tolerances.
Pins are independent-sum refs: energy in eV, forces in eV/A.
"""

from __future__ import annotations

import hashlib
import json
import os
import socket
import subprocess
import sys
from pathlib import Path

TERRA_HOSTS = {"rgam5terra", "rg.terra"}
ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "CppCore" / "tests" / "data" / "expr"

ENERGY_TOL = 1e-14
FORCE_TOL = 1e-12

REQUIRED = (
    "positions.npy",
    "numbers.npy",
    "box.npy",
    "lj_energy.npy",
    "lj_forces.npy",
    "morse_energy.npy",
    "morse_forces.npy",
    "identity_lj_energy.npy",
    "identity_lj_forces.npy",
    "half_lj_plus_morse_energy.npy",
    "half_lj_plus_morse_forces.npy",
    "two_lj_minus_lj_energy.npy",
    "two_lj_minus_lj_forces.npy",
    "half_paren_lj_morse_energy.npy",
    "half_paren_lj_morse_forces.npy",
)

OPTIONAL_D3 = (
    "d3_energy.npy",
    "d3_forces.npy",
    "half_lj_plus_d3_energy.npy",
    "half_lj_plus_d3_forces.npy",
)


def _require_terra() -> None:
    host = socket.gethostname().split(".")[0]
    if host not in TERRA_HOSTS and os.environ.get("EXPR_ALLOW_REGEN") != "1":
        sys.exit(f"regen_expr_goldens.py runs on rg.terra only (got {host})")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def find_dump() -> Path:
    env = os.environ.get("DUMP_EXPR_GOLDENS")
    if env:
        p = Path(env)
        if p.is_file():
            return p
        sys.exit(f"DUMP_EXPR_GOLDENS is not a file: {p}")
    candidates = [
        ROOT / "bbdir-expr" / "CppCore" / "dump_expr_goldens",
        ROOT / "bbdir-expr" / "dump_expr_goldens",
    ]
    for cand in candidates:
        if cand.is_file():
            return cand
    sys.exit(
        "dump_expr_goldens missing; meson compile -C bbdir-expr dump_expr_goldens "
        "or set DUMP_EXPR_GOLDENS"
    )


def write_manifest() -> None:
    files: dict[str, dict] = {}
    names = list(REQUIRED)
    for name in OPTIONAL_D3:
        if (DATA / name).is_file():
            names.append(name)
    for name in names:
        path = DATA / name
        if not path.is_file():
            sys.exit(f"missing pin after regen: {path}")
        files[name] = {"sha256": sha256_file(path), "bytes": path.stat().st_size}
    cases = [
        "identity_lj",
        "half_lj_plus_morse",
        "two_lj_minus_lj",
        "half_paren_lj_morse",
    ]
    if (DATA / "half_lj_plus_d3_energy.npy").is_file():
        cases.append("half_lj_plus_d3")
    man = {
        "host_only": "rg.terra",
        "regen": "scripts/regen_expr_goldens.py",
        "fixture": "two_atom_ar_1p5A",
        "units": {"energy": "eV", "forces": "eV/A"},
        "tolerances": {
            "energy": ENERGY_TOL,
            "forces": FORCE_TOL,
            "source": "rgpot-2wdt: energy 1e-14 eV, forces 1e-12 eV/A vs independent sum",
        },
        "cases": cases,
        "files": files,
    }
    (DATA / "MANIFEST.json").write_text(json.dumps(man, indent=2) + "\n")
    print("MANIFEST.json")


def main() -> int:
    _require_terra()
    DATA.mkdir(parents=True, exist_ok=True)
    dump = find_dump()
    subprocess.run([str(dump)], cwd=ROOT, check=True)
    write_manifest()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
