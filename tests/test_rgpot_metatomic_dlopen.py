"""Ship the Metatomic **dlopen** path: load installed engine, real forces."""

from __future__ import annotations

import math
import os
import shutil
import subprocess
from pathlib import Path

import numpy as np
import pytest

rgpot = pytest.importorskip("rgpot")


def _engine_path() -> Path | None:
    p = rgpot.default_metatomic_engine_path()
    if p:
        return Path(p)
    # also search site-packages mesonpy.libs
    root = Path(rgpot.__file__).resolve().parent
    for c in (
        root.parent / ".rgpot.mesonpy.libs" / "libmetatomic_engine.so",
        root / "lib" / "libmetatomic_engine.so",
    ):
        if c.is_file():
            return c
    env = os.environ.get("RGPOT_METATOMIC_ENGINE")
    if env and Path(env).is_file():
        return Path(env)
    return None


def _model_path() -> Path | None:
    env = os.environ.get("RGPOT_METATOMIC_MODEL")
    if env and Path(env).is_file():
        return Path(env)
    # common local locations (terra cookbook)
    candidates = [
        Path.home()
        / "Git/Github/lab-cosmo/atomistic-cookbook/examples/eon-pet-neb/models/pet-mad-xs-v1.5.0.pt",
        Path.home()
        / "Git/Github/epfl/pixi_envs/atomistic-cookbook/atomistic-cookbook/examples/eon-pet-neb/models/pet-mad-xs-v1.5.0.pt",
    ]
    for c in candidates:
        if c.is_file():
            return c
    return None


def test_has_dlopen_frontend():
    assert rgpot.has_metatomic_dlopen is True
    assert callable(rgpot.evaluate_metatomic_dlopen)
    assert callable(rgpot.evaluate_metatomic)


def test_engine_plugin_in_install_or_env():
    eng = _engine_path()
    assert eng is not None and eng.is_file(), (
        "libmetatomic_engine.so missing from package/env — "
        "wheel must install the engine plugin (not LJ-only packaging)"
    )


def test_engine_is_portable_plugin():
    eng = _engine_path()
    assert eng is not None
    if not shutil.which("readelf"):
        pytest.skip("readelf missing")
    out = subprocess.check_output(["readelf", "-d", str(eng)], text=True)
    # Must not need eOn client
    assert "libeonclib" not in out
    assert "eonclib" not in out
    for line in out.splitlines():
        low = line.lower()
        if "runpath" not in low and "rpath" not in low:
            continue
        for bad in ("/bbdir", "/.mesonpy", "/build-pyeon", "eOn-pyeon"):
            assert bad not in line, line


def test_dlopen_force_evaluation():
    eng = _engine_path()
    model = _model_path()
    assert eng is not None, "engine required"
    if model is None:
        pytest.fail(
            "No metatomic model file; set RGPOT_METATOMIC_MODEL to a .pt path "
            "(cannot skip — plan requires real force through dlopen)"
        )

    # Ensure engine loadable without build-tree LD_LIBRARY_PATH
    os.environ.pop("LD_LIBRARY_PATH", None)
    os.environ.pop("LIBRARY_PATH", None)
    os.environ["RGPOT_METATOMIC_ENGINE"] = str(eng)

    # Minimal H2-like geometry (Angstrom); PET-MAD accepts H/C/N/O/…
    positions = np.array(
        [
            [0.0, 0.0, 0.0],
            [0.74, 0.0, 0.0],
        ],
        dtype=np.float64,
    )
    atom_types = np.array([1, 1], dtype=np.int32)  # H, H
    box = np.eye(3, dtype=np.float64) * 20.0

    energy, forces, variance = rgpot.evaluate_metatomic(
        positions,
        atom_types,
        box,
        model_path=str(model),
        engine_path=str(eng),
        device="cpu",
    )
    assert math.isfinite(float(energy)), energy
    assert forces.shape == (2, 3), forces.shape
    assert np.all(np.isfinite(forces)), forces
    print("RGPOT_DLOPEN_FORCE_OK", float(energy), forces.shape)
