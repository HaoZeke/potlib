#!/usr/bin/env python3
"""Geometry minimization: ASE local optimizer vs anneal's SOTA portfolio.

Both optimizers drive the *same* rgpot Lennard-Jones potential (served by
``potserv`` and reached through :class:`RgpotCalculator`), so this is an
apples-to-apples comparison on one potential energy surface.

The system is a 7-atom LJ cluster (reduced units, sigma = epsilon = 1). Its PES
has many local minima; the global minimum is the pentagonal bipyramid at
E = -16.505384. From a disordered start:

* ASE ``LBFGS`` is a *local* optimizer: it slides downhill into whichever basin
  the start sits in -- often not the global one.
* anneal ``global_optimize`` is the *global* portfolio of the INFORMS Journal on
  Computing paper (Thompson-allocated QMC restart descent, basin hopping,
  differential evolution, GLE-Langevin, generalized SA, parallel tempering,
  q-Gaussian HMC, ... under one budget). It is handed the rgpot forces as the
  gradient, enabling the gradient arms and final polish.

The portfolio should reach an energy at least as low as ASE's local result, and
in fact recover the known global minimum.

Run frictionlessly via:  ``pixi run -e rpctest ase-anneal-compare``
(or: ``RGPOT_POTSERV_BIN=<path/to/potserv> python tests/ase_vs_anneal.py``).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import numpy as np
from ase import Atoms
from ase.optimize import LBFGS
from scipy.optimize import dual_annealing

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "CppCore" / "rgpot" / "rpc"))
from ase_calculator import RgpotCalculator  # noqa: E402

import anneal  # noqa: E402

N_ATOMS = 7
LJ7_GLOBAL_MIN = -16.505384  # reduced LJ units (Wales/Doye database)
BOX = [40.0, 40.0, 40.0]     # >> cutoff (15) so the cluster sees no periodic image


def random_start(seed: int) -> np.ndarray:
    rng = np.random.RandomState(seed)
    return rng.uniform(-2.3, 2.3, size=(N_ATOMS, 3))


def min_pair_distance(flat) -> float:
    p = np.asarray(flat, dtype=float).reshape(N_ATOMS, 3)
    dmin = np.inf
    for i in range(N_ATOMS):
        for j in range(i + 1, N_ATOMS):
            dmin = min(dmin, float(np.linalg.norm(p[i] - p[j])))
    return dmin


def make_atoms(positions) -> Atoms:
    return Atoms(numbers=[0] * N_ATOMS, positions=np.asarray(positions).reshape(N_ATOMS, 3), cell=BOX)


def run_ase(calc, start) -> tuple[float, int]:
    """Local relaxation with ASE LBFGS. Returns (energy, n_force_evals)."""
    atoms = make_atoms(start)
    atoms.calc = calc
    opt = LBFGS(atoms, logfile=None)
    opt.run(fmax=1e-3, steps=500)
    return float(atoms.get_potential_energy()), opt.get_number_of_steps()


def run_scipy_sa(calc, start, maxiter, seed) -> tuple[float, int]:
    """Global SA baseline: scipy.optimize.dual_annealing (generalized SA + local
    search), handed the rgpot forces as the analytic Jacobian."""
    scratch = make_atoms(start)
    scratch.calc = calc

    def energy(x):
        scratch.set_positions(np.asarray(x).reshape(N_ATOMS, 3))
        return float(scratch.get_potential_energy())

    def jac(x):
        scratch.set_positions(np.asarray(x).reshape(N_ATOMS, 3))
        return (-scratch.get_forces()).ravel()

    dim = N_ATOMS * 3
    bounds = list(zip([-3.0] * dim, [3.0] * dim))
    res = dual_annealing(
        energy, bounds, x0=np.asarray(start).ravel(), maxiter=maxiter, seed=seed,
        minimizer_kwargs={"method": "L-BFGS-B", "jac": jac},
    )
    return float(res.fun), int(res.nfev)


def run_portfolio(calc, start, budget, seed) -> dict:
    """Global optimization with anneal's portfolio, using rgpot forces as grad."""
    scratch = make_atoms(start)
    scratch.calc = calc

    def energy(x: np.ndarray) -> float:
        scratch.set_positions(np.asarray(x).reshape(N_ATOMS, 3))
        return float(scratch.get_potential_energy())

    def gradient(x: np.ndarray) -> np.ndarray:
        scratch.set_positions(np.asarray(x).reshape(N_ATOMS, 3))
        return (-scratch.get_forces()).ravel()  # grad E = -F

    dim = N_ATOMS * 3
    low = np.full(dim, -3.0)
    high = np.full(dim, 3.0)
    return anneal.global_optimize(energy, low, high, budget, seed=seed, grad_fn=gradient)


def main() -> int:
    server_bin = os.environ.get("RGPOT_POTSERV_BIN")
    if not server_bin:
        print("set RGPOT_POTSERV_BIN to the potserv binary", file=sys.stderr)
        return 2

    start = random_start(seed=1)
    budget = 15000

    with RgpotCalculator.spawn(server_bin=server_bin, potential="LJ") as calc:
        atoms0 = make_atoms(start)
        atoms0.calc = calc
        e_start = atoms0.get_potential_energy()

        e_ase, n_ase = run_ase(calc, start)
        e_scipy, nfev_scipy = run_scipy_sa(calc, start, maxiter=400, seed=11)
        res = run_portfolio(calc, start, budget, seed=11)

    e_anneal = float(res["best_val"])
    top_arms = sorted(dict(res["arm_pulls"]).items(), key=lambda kv: -kv[1])[:4]

    print("Geometry minimization of the rgpot LJ7 cluster (reduced units)")
    print(f"  start energy                  : {e_start:.4f}")
    print(f"  known LJ7 global minimum      : {LJ7_GLOBAL_MIN:.6f}")
    print()
    print(f"  ASE LBFGS (local)             : energy = {e_ase:11.6f}   ({n_ase} force steps)")
    print(f"  scipy dual_annealing (global) : energy = {e_scipy:11.6f}   ({nfev_scipy} fevals)")
    print(f"  anneal portfolio (global,SOTA): energy = {e_anneal:11.6f}   "
          f"({res['n_evals']} evals + {res['n_grads']} grads)")
    print()
    print(f"  portfolio min pair distance   : {min_pair_distance(res['best_pos']):.4f} "
          f"(LJ pair minimum ~1.1225)")
    print(f"  top portfolio arms (pulls)    : {top_arms}")
    print()
    print(f"  portfolio - ASE local         = {e_anneal - e_ase:+.6f}  (<=0: global beat local)")
    print(f"  portfolio - scipy dual_anneal = {e_anneal - e_scipy:+.6f}")
    print(f"  portfolio - known global      = {e_anneal - LJ7_GLOBAL_MIN:+.6f}")

    beats_local = e_anneal <= e_ase + 1e-3
    matches_or_beats_scipy = e_anneal <= e_scipy + 1e-3
    reached_global = abs(e_anneal - LJ7_GLOBAL_MIN) < 0.05
    ok = beats_local and matches_or_beats_scipy and reached_global
    print("  RESULT:",
          "PASS -- anneal portfolio reached the LJ7 global minimum, <= ASE local and scipy SA"
          if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
