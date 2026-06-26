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
import matplotlib
import numpy as np
from ase import Atoms

matplotlib.use("Agg")

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


def _render_landscape(job_dir: Path, out: Path, *, energy_cap: float | None) -> None:
    """Render the (s, d) landscape, capping energies so the repulsive r^-12
    spike of the perturbed start does not flatten the colormap over the well.

    Mirrors ``rgpycrumbs eon plt-min --plot-type landscape`` but clips the
    energies fed to the surface fit and colorbar (the CLI has no cap flag)."""
    from chemparseplot.parse.eon.min_trajectory import load_min_trajectory
    from chemparseplot.parse.neb_utils import (
        calculate_landscape_coords,
        compute_synthetic_gradients,
    )
    from chemparseplot.plot.optimization import (
        annotate_endpoint,
        create_landscape_axes,
        plot_optimization_landscape,
        project_landscape_path,
    )
    from chemparseplot.plot.structs import convert_energy
    from chemparseplot.plot.theme import get_theme, setup_global_theme

    try:
        from rgpycrumbs._aux import _import_from_parent_env

        ira_mod = _import_from_parent_env("ira_mod")
    except ImportError:
        ira_mod = None
    ira_instance = ira_mod.IRA() if ira_mod else None

    traj = load_min_trajectory(job_dir, prefix="minimization")
    rmsd_a, rmsd_b = calculate_landscape_coords(
        traj.atoms_list, ira_instance, 14.0,
        ref_a=traj.initial_atoms, ref_b=traj.final_atoms,
    )
    energies = convert_energy(traj.dat_df["energy"].to_numpy(), "eV")
    n = min(len(rmsd_a), len(energies))
    rmsd_a, rmsd_b, energies = rmsd_a[:n], rmsd_b[:n], energies[:n]

    # Cap the few high-energy frames near the perturbed start so the colorbar
    # resolves the bound LJ well instead of the r^-12 wall. Default: a 12 eV
    # window above the converged minimum.
    cap = energy_cap if energy_cap is not None else float(np.min(energies)) + 12.0
    energies = np.minimum(energies, cap)

    f_para = -np.gradient(energies)
    grad_a, grad_b = compute_synthetic_gradients(rmsd_a, rmsd_b, f_para)

    theme = get_theme("ruhi")
    setup_global_theme(theme)
    fig, ax, _ = create_landscape_axes(dpi=200, has_strip=False, theme=theme)
    plot_optimization_landscape(
        ax, rmsd_a, rmsd_b, grad_a, grad_b, energies,
        project_path=True, method="grad_matern",
        cmap=theme.cmap_landscape, label_mode="optimization", energy_unit="eV",
    )
    px, py, _ = project_landscape_path(rmsd_a, rmsd_b, project_path=True)
    annotate_endpoint(ax, float(px[0]), float(py[0]), "Init", boxed=True)
    annotate_endpoint(ax, float(px[-1]), float(py[-1]), "Min", boxed=True)
    fig.savefig(out, dpi=200, bbox_inches="tight")


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

    # 3. Render the (s, d) optimization landscape with the trajectory overlaid,
    #    capping energies so the colorbar resolves the bound well.
    _render_landscape(run, out, energy_cap=None)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
