"""rgpot — potential energy surfaces for atomistic simulation.

Core: Lennard-Jones. Metatomic path uses **dlopen** of ``libmetatomic_engine.so``
(not a fat-only link): install the engine next to the package or set
``RGPOT_METATOMIC_ENGINE`` / pass ``engine_path=``.
"""

from __future__ import annotations

import os
from pathlib import Path

from rgpot._core import (
    LJPot,
    __version__,
    evaluate_lj,
    evaluate_metatomic_dlopen,
    has_metatomic_dlopen,
)


def default_metatomic_engine_path() -> str | None:
    """Return package-bundled libmetatomic_engine.so if present."""
    here = Path(__file__).resolve().parent
    candidates = [
        here / "lib" / "libmetatomic_engine.so",
        here.parent / ".rgpot.mesonpy.libs" / "libmetatomic_engine.so",
        here / "libmetatomic_engine.so",
    ]
    for c in candidates:
        if c.is_file():
            return str(c)
    env = os.environ.get("RGPOT_METATOMIC_ENGINE") or os.environ.get(
        "METATOMIC_ENGINE"
    )
    if env and Path(env).is_file():
        return env
    return None


def evaluate_metatomic(
    positions,
    atom_types,
    box,
    *,
    model_path: str,
    engine_path: str | None = None,
    device: str = "cpu",
):
    """Force evaluation through MetatomicDlopen (real engine plugin)."""
    eng = engine_path or default_metatomic_engine_path() or ""
    return evaluate_metatomic_dlopen(
        positions, atom_types, box, model_path, eng, device
    )


__all__ = [
    "LJPot",
    "evaluate_lj",
    "evaluate_metatomic",
    "evaluate_metatomic_dlopen",
    "default_metatomic_engine_path",
    "has_metatomic_dlopen",
    "__version__",
]
