#!/usr/bin/env python3
"""A 2D Lennard-Jones double-well surface: local optimizer vs anneal global.

Densely evaluates the rgpot LJ energy of a probe atom moving in the x-y plane
near a lone atom (a shallow well) and a nearby pair (a deeper well), giving a
genuine 2D potential energy surface with two basins. Unlike the (s, d)
trajectory landscape -- which fits a surface from a sparse 1D descent -- this
samples the whole region, so the surface covers the full extent and is smooth.

Overlaid:
  * ASE LBFGS started in the shallow basin -> stops there (local).
  * anneal's global portfolio -> the deep basin (global).

Run: ``RGPOT_POTSERV_BIN=<potserv> python tests/pes_double_well.py [out.png]``
(or ``pixi run -e rpctest pes-double-well``).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from ase import Atoms  # noqa: E402
from ase.constraints import FixAtoms, FixedPlane  # noqa: E402
from ase.optimize import LBFGS  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "CppCore" / "rgpot" / "rpc"))
from ase_calculator import RgpotCalculator  # noqa: E402

import anneal  # noqa: E402

# Lone atom (shallow well) on the left; a bound pair (deeper well) on the right.
FIXED = np.array([[-3.0, 0.0, 0.0], [3.0, 0.56, 0.0], [3.0, -0.56, 0.0]])
CELL = [40.0, 40.0, 40.0]
X_RANGE, Y_RANGE = (-5.5, 5.5), (-3.5, 3.5)
E_CEILING = 0.5  # clip the repulsive cores so the wells are legible


def _atoms(probe_xy) -> Atoms:
    pos = np.vstack([[probe_xy[0], probe_xy[1], 0.0], FIXED])
    return Atoms(numbers=[0] * 4, positions=pos, cell=CELL)


def main() -> int:
    server_bin = os.environ.get("RGPOT_POTSERV_BIN")
    if not server_bin:
        print("set RGPOT_POTSERV_BIN to the potserv binary", file=sys.stderr)
        return 2
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
        "docs/orgmode/_static/pes_double_well.png")
    out.parent.mkdir(parents=True, exist_ok=True)

    with RgpotCalculator.spawn(server_bin=server_bin, potential="LJ") as calc:
        probe = _atoms((0.0, 0.0))
        probe.calc = calc

        def energy_at(x, y):
            p = probe.get_positions()
            p[0] = [x, y, 0.0]
            probe.set_positions(p)
            return probe.get_potential_energy()

        # Dense 2D grid -> the full surface.
        xs = np.linspace(*X_RANGE, 140)
        ys = np.linspace(*Y_RANGE, 100)
        zz = np.empty((ys.size, xs.size))
        for j, y in enumerate(ys):
            for i, x in enumerate(xs):
                zz[j, i] = energy_at(x, y)
        zz = np.minimum(zz, E_CEILING)

        # Local relaxation started just outside the shallow (left) well, probe
        # in the x-y plane. The lone-atom well is a flat ring, so start close so
        # the descent settles in it rather than drifting along the ring.
        loc = _atoms((-4.0, 0.2))
        loc.calc = calc
        loc.set_constraint([FixAtoms(indices=[1, 2, 3]), FixedPlane(0, [0, 0, 1])])
        LBFGS(loc, logfile=None).run(fmax=1e-3, steps=200)
        loc_xy = loc.get_positions()[0, :2]
        loc_e = float(loc.get_potential_energy())

        # Global portfolio over the (x, y) probe position.
        def obj(v):
            return energy_at(float(v[0]), float(v[1]))

        res = anneal.global_optimize(
            obj, np.array([X_RANGE[0], Y_RANGE[0]]),
            np.array([X_RANGE[1], Y_RANGE[1]]), 4000, seed=7)
        glob_xy = np.asarray(res["best_pos"])
        glob_e = float(res["best_val"])

    # Plot the surface and both results.
    fig, ax = plt.subplots(figsize=(6.4, 4.2), dpi=200)
    levels = np.linspace(np.min(zz), E_CEILING, 40)
    cf = ax.contourf(xs, ys, zz, levels=levels, cmap="viridis")
    ax.contour(xs, ys, zz, levels=12, colors="white", linewidths=0.3, alpha=0.5)
    fig.colorbar(cf, ax=ax, label="energy (eV)")
    ax.scatter(*FIXED[:, :2].T, marker="x", c="white", s=40, label="fixed atoms")
    ax.plot(*loc_xy, marker="o", ms=11, color="tab:red",
            label=f"ASE LBFGS, local ({loc_e:.2f} eV)")
    ax.plot(*glob_xy, marker="*", ms=18, color="tab:orange",
            label=f"anneal global ({glob_e:.2f} eV)")
    ax.set_xlabel("x (Angstrom)")
    ax.set_ylabel("y (Angstrom)")
    ax.set_title("rgpot LJ double well: local stops shallow, global finds deep")
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.18),
              fontsize="small", framealpha=0.9, ncol=3)
    ax.set_aspect("equal")
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")

    print(f"local  ({loc_xy[0]:.2f},{loc_xy[1]:.2f})  E={loc_e:.4f}")
    print(f"global ({glob_xy[0]:.2f},{glob_xy[1]:.2f})  E={glob_e:.4f}")
    print(f"wrote {out}")
    return 0 if glob_e < loc_e - 0.05 else 1


if __name__ == "__main__":
    raise SystemExit(main())
