#!/usr/bin/env python3
"""Visualize an EON minimization on the (s, d) optimization landscape.

Runs a real ``eonclient`` minimization of a Lennard-Jones cluster (reduced
units, the same LJ surface rgpot serves) and renders chemparseplot's gradient
-enhanced 2D landscape -- progress ``s`` toward the minimum vs lateral deviation
``d`` -- with the trajectory overlaid, via ``rgpycrumbs eon plt-min``.

This is the EON-native counterpart to ``tests/ase_vs_anneal.py``: where that
script compares optimizers numerically on the rgpot LJ surface, this one shows
the actual minimization path an EON optimizer walks, on the surface it walked.

Run in the dedicated env (eon + the dev chemparseplot/rgpycrumbs):

    pixi run -e eonviz eon-min-landscape

The figure lands at ``docs/orgmode/_static/eon_min_landscape.png`` by default.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import ase.io as aseio
import numpy as np
from ase import Atoms

MIN_CONFIG = """[Main]
job = minimization
random_seed = 1

[Potential]
potential = lj

[Optimizer]
opt_method = lbfgs
converged_force = 0.001
max_iterations = 1000

[Debug]
write_movies = true
"""


def _write_case(job_dir: Path, atoms: Atoms) -> None:
    job_dir.mkdir(parents=True, exist_ok=True)
    aseio.write(job_dir / "pos.con", atoms, format="eon")
    (job_dir / "config.ini").write_text(MIN_CONFIG)


def _run_eon(job_dir: Path) -> None:
    subprocess.run(["eonclient"], cwd=job_dir, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build-eon-min")
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(
        "docs/orgmode/_static/eon_min_landscape.png")
    out.parent.mkdir(parents=True, exist_ok=True)

    # 1. Relax a random compact LJ7 cluster to a clean minimum basin.
    rng = np.random.RandomState(2)
    seed_cluster = Atoms(symbols=["H"] * 7,
                         positions=rng.uniform(-1.6, 1.6, size=(7, 3)),
                         cell=[30, 30, 30])
    basin = work / "basin"
    _write_case(basin, seed_cluster)
    _run_eon(basin)
    minimum = aseio.read(basin / "min.con")

    # 2. Gently perturb that minimum and re-minimize, recording the movie. The
    #    perturbation keeps energies inside the LJ well so the trajectory is a
    #    clean descent the landscape can fit.
    perturbed = minimum.copy()
    perturbed.rattle(stdev=0.2, seed=11)
    run = work / "min_run"
    _write_case(run, perturbed)
    _run_eon(run)

    # 3. Render the (s, d) optimization landscape with the trajectory overlaid.
    cmd = [
        sys.executable, "-m", "rgpycrumbs.eon.plt_min",
        "--job-dir", str(run), "--plot-type", "landscape", "-o", str(out),
    ]
    subprocess.run(cmd, check=True)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
