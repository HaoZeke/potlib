#!/usr/bin/env python3
"""Time in-process rgpot LJPot vs pyeonclient Matter on one LJ fixture.

Required race (rg.terra, repo fixture, no invented pot settings):

- Fixture: ExprPotTest two-atom Ar (1.5 A pair, 40 A cell). Same geometry
  as regen_expr_goldens.py / ExprPotTest identity.
- A: rgpot.LJPot operator() (nanobind calls Potential::operator()).
- B: pyeonclient Matter with PotType.LJ (in-process, not potserv RPC).
  eOn makePotential(LJ) constructs rgpot::LJPot with LJConfig{}.
- C: optional ExprPot("lj") when the build exports it. Wheel builds
  ship with_expr off, so C is skipped unless the module is present.

Prints wall ns/call. Does not seed numbers. Does not time XcKernel
(XcKernel is not a Potential).
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time
from typing import Any

import numpy as np

TERRA_HOSTS = {"rgam5terra", "rg.terra"}

# ExprPotTest two-atom Ar identity fixture (Angstrom, eV).
POSITIONS = np.array([[0.0, 0.0, 0.0], [1.5, 0.0, 0.0]], dtype=np.float64)
ATOM_TYPES = np.array([18, 18], dtype=np.int32)
ATOMIC_NUMBERS = np.array([18, 18], dtype=np.int64)
BOX = np.eye(3, dtype=np.float64) * 40.0
MASSES = np.array([39.948, 39.948], dtype=np.float64)


def _require_terra() -> None:
    host = socket.gethostname().split(".")[0]
    if host not in TERRA_HOSTS and os.environ.get("TIME_LJ_ALLOW_LOCAL") != "1":
        sys.exit(
            f"time_inprocess_lj_rgpot_vs_pyeonclient.py runs on rg.terra "
            f"only (got {host})"
        )


def _ns_per_call(n_calls: int, elapsed_ns: int) -> float:
    if n_calls <= 0:
        raise SystemExit("n_calls must be positive")
    return elapsed_ns / float(n_calls)


def time_rgpot_ljpot(n_calls: int, warmup: int) -> tuple[float, float]:
    import rgpot

    pot = rgpot.LJPot()
    for _ in range(warmup):
        energy, _forces, _var = pot(POSITIONS, ATOM_TYPES, BOX)
    energy, _forces, _var = pot(POSITIONS, ATOM_TYPES, BOX)
    t0 = time.perf_counter_ns()
    for _ in range(n_calls):
        energy, _forces, _var = pot(POSITIONS, ATOM_TYPES, BOX)
    elapsed = time.perf_counter_ns() - t0
    return float(energy), _ns_per_call(n_calls, elapsed)


def time_pyeonclient_matter(n_calls: int, warmup: int) -> tuple[float, float]:
    import pyeonclient as pyec

    params = pyec.Parameters()
    params.potential = pyec.PotType.LJ
    params.quiet = True
    if hasattr(params, "write_log"):
        params.write_log = False
    pot = pyec.make_potential(pyec.PotType.LJ, params)
    matter = pyec.Matter(pot, params)
    matter.resize(2)
    matter.cell = BOX
    matter.positions = POSITIONS
    matter.masses = MASSES
    matter.atomic_numbers = ATOMIC_NUMBERS
    if hasattr(matter, "fixed"):
        matter.fixed = np.array([0, 0], dtype=np.int64)
    matter.periodic = True

    pos = POSITIONS.copy()
    for _ in range(warmup):
        matter.positions = pos
        _ = np.asarray(matter.forces)
    energy = float(matter.potential_energy)
    t0 = time.perf_counter_ns()
    for _ in range(n_calls):
        # setPositions marks recomputePotential so each iteration is a
        # real force call, not a cached Matter read.
        matter.positions = pos
        _ = np.asarray(matter.forces)
    elapsed = time.perf_counter_ns() - t0
    return energy, _ns_per_call(n_calls, elapsed)


def time_exprpot_lj(n_calls: int, warmup: int) -> tuple[float, float] | None:
    try:
        import rgpot
    except ImportError:
        return None
    expr_cls = getattr(rgpot, "ExprPot", None)
    if expr_cls is None:
        return None
    terms: Any
    try:
        terms = [("lj", rgpot.LJPot())]
        pot = expr_cls("lj", terms)
    except TypeError:
        return None
    for _ in range(warmup):
        energy, _forces, _var = pot(POSITIONS, ATOM_TYPES, BOX)
    energy, _forces, _var = pot(POSITIONS, ATOM_TYPES, BOX)
    t0 = time.perf_counter_ns()
    for _ in range(n_calls):
        energy, _forces, _var = pot(POSITIONS, ATOM_TYPES, BOX)
    elapsed = time.perf_counter_ns() - t0
    return float(energy), _ns_per_call(n_calls, elapsed)


def _pkg_version(mod: Any) -> str:
    return str(getattr(mod, "__version__", "unknown"))


def main() -> int:
    _require_terra()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n-calls", type=int, default=20000)
    parser.add_argument("--warmup", type=int, default=50)
    args = parser.parse_args()
    if args.n_calls < 1 or args.warmup < 0:
        sys.exit("invalid --n-calls / --warmup")

    try:
        import rgpot
    except ImportError as exc:
        sys.exit(f"rgpot is required for A: {exc}")
    try:
        import pyeonclient as pyec
    except ImportError as exc:
        sys.exit(f"pyeonclient is required for B: {exc}")

    host = socket.gethostname().split(".")[0]
    a_energy, a_ns = time_rgpot_ljpot(args.n_calls, args.warmup)
    b_energy, b_ns = time_pyeonclient_matter(args.n_calls, args.warmup)
    c = time_exprpot_lj(args.n_calls, args.warmup)

    print(f"host={host}")
    print("fixture=exprpot_two_atom_ar")
    print(f"n_atoms={POSITIONS.shape[0]}")
    print(f"n_calls={args.n_calls}")
    print(f"warmup={args.warmup}")
    print(f"rgpot_version={_pkg_version(rgpot)}")
    print(f"pyeonclient_version={_pkg_version(pyec)}")
    print(f"A_ljpot_energy_eV={a_energy:.12e}")
    print(f"B_matter_energy_eV={b_energy:.12e}")
    print(f"A_ljpot_ns_per_call={a_ns:.3f}")
    print(f"B_pyeonclient_matter_ns_per_call={b_ns:.3f}")
    if c is None:
        print("C_exprpot_lj_ns_per_call=skipped")
        print("C_note=with_expr not exported by this rgpot build")
    else:
        c_energy, c_ns = c
        print(f"C_exprpot_lj_energy_eV={c_energy:.12e}")
        print(f"C_exprpot_lj_ns_per_call={c_ns:.3f}")
        if a_ns > 0.0:
            print(f"C_over_A_factor={c_ns / a_ns:.3f}")
    print("note=XcKernel is not a Potential; not timed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
