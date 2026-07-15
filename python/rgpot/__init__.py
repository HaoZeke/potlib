"""rgpot — potential energy surfaces for atomistic simulation.

Core: Lennard-Jones. Metatomic path uses **dlopen** of a portable
``libmetatomic_engine.so`` (stable C ABI). Engines are multi-ABI:

  ``rgpot/lib/torch-X.Y/libmetatomic_engine.so``

selected from the installed ``torch`` major (same layout as metatomic-torch).
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


def _torch_major() -> str | None:
    """Return installed torch X.Y, or None if torch is not importable."""
    try:
        import torch

        v = torch.__version__.split("+", 1)[0]
        parts = v.split(".")
        return f"{parts[0]}.{parts[1]}"
    except Exception:
        return None


def default_metatomic_engine_path() -> str | None:
    """Return package-bundled engine matching installed torch ABI if possible."""
    here = Path(__file__).resolve().parent
    maj = _torch_major()
    candidates: list[Path] = []
    # Multi-ABI layout first (rgpot/lib/torch-X.Y/) — preferred product path
    if maj:
        candidates.append(here / "lib" / f"torch-{maj}" / "libmetatomic_engine.so")
    lib_root = here / "lib"
    if lib_root.is_dir():
        for d in sorted(lib_root.glob("torch-*")):
            candidates.append(d / "libmetatomic_engine.so")
    # Legacy single-engine layouts (last resort)
    candidates.extend(
        [
            here / "lib" / "libmetatomic_engine.so",
            here.parent / ".rgpot.mesonpy.libs" / "libmetatomic_engine.so",
            here / "libmetatomic_engine.so",
        ]
    )

    for c in candidates:
        if c.is_file():
            return str(c)

    env = os.environ.get("RGPOT_METATOMIC_ENGINE") or os.environ.get(
        "METATOMIC_ENGINE"
    )
    if env and Path(env).is_file():
        return env
    return None


def available_metatomic_engine_abis() -> list[str]:
    """List torch-X.Y majors for which a bundled engine is present."""
    here = Path(__file__).resolve().parent / "lib"
    out: list[str] = []
    if not here.is_dir():
        return out
    for d in sorted(here.glob("torch-*")):
        if (d / "libmetatomic_engine.so").is_file():
            out.append(d.name.removeprefix("torch-"))
    return out


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
    "available_metatomic_engine_abis",
    "has_metatomic_dlopen",
    "__version__",
]
