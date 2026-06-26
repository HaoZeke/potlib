#!/usr/bin/env python3
"""Visualize EON minimizations on the (s, d) optimization landscape.

Runs several ``eonclient`` minimizations of a Lennard-Jones cluster (reduced
units, the same LJ surface rgpot serves) from different random starts, then
overlays two that fall into *different* basins -- one reaching the global LJ7
minimum, one trapped in a higher local minimum -- on chemparseplot's
gradient-enhanced 2D landscape (progress ``s`` toward the minimum vs lateral
deviation ``d``), via ``rgpycrumbs eon plt-min``.

Why overlay two runs: a single minimization is a near-1D descent, so its
landscape surface is featureless off the path. Two trajectories landing in
different basins make the local-vs-global structure visible -- the paths diverge
to different (s, d) endpoints at different energies.

The energy colormap is capped with ``--energy-cap-window`` so a repulsive start
frame does not flatten the color scale over the bound well.

Run in the dedicated env (eon + the dev chemparseplot/rgpycrumbs):

    pixi run -e eonviz eon-min-landscape
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import ase.io as aseio
import numpy as np
import readcon
from ase import Atoms

MIN_CONFIG = """[Main]
job = minimization
random_seed = 1

[Potential]
potential = lj

[Optimizer]
opt_method = lbfgs
converged_force = 0.001
max_iterations = 2000

[Debug]
write_movies = true
"""


def _minimize(job_dir: Path, seed: int) -> float:
    """Relax a random LJ7 cluster with eonclient; return the final energy."""
    job_dir.mkdir(parents=True, exist_ok=True)
    rng = np.random.RandomState(seed)
    atoms = Atoms(symbols=["H"] * 7,
                  positions=rng.uniform(-1.7, 1.7, size=(7, 3)),
                  cell=[30, 30, 30])
    aseio.write(job_dir / "pos.con", atoms, format="eon")
    (job_dir / "config.ini").write_text(MIN_CONFIG)
    subprocess.run(["eonclient"], cwd=job_dir, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # The eon movie .con frames carry per-iteration metadata (energy, convergence,
    # step_size); read the converged energy straight from the last frame.
    final = readcon.read_con(str(job_dir / "minimization.con"))[-1]
    return float(final.energy)


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build-eon-min")
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(
        "docs/orgmode/_static/eon_min_landscape.png")
    out.parent.mkdir(parents=True, exist_ok=True)

    # 1. Minimize from several random starts; record the basin each reaches.
    runs = []
    for seed in range(8):
        d = work / f"seed{seed}"
        runs.append((seed, d, _minimize(d, seed)))
    runs.sort(key=lambda r: r[2])
    best = runs[0]                                   # lowest energy (global-most)
    local = next((r for r in reversed(runs) if r[2] - best[2] > 0.2), None)

    # 2. Overlay the global-reaching and (if found) a higher local trajectory.
    job_args, labels = ["--job-dir", str(best[1])], ["--label", f"global ({best[2]:.3f})"]
    if local is not None:
        job_args += ["--job-dir", str(local[1])]
        labels += ["--label", f"local ({local[2]:.3f})"]

    cmd = [
        sys.executable, "-m", "rgpycrumbs.eon.plt_min",
        *job_args, *labels,
        "--plot-type", "landscape",
        "--energy-cap-window", "12",
        "-o", str(out),
    ]
    subprocess.run(cmd, check=True)
    print(f"wrote {out}: global={best[2]:.4f}"
          + (f", local={local[2]:.4f}" if local else " (no distinct local basin found)"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
