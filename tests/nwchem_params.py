#!/usr/bin/env python3
"""Helpers for NWChemParams / PotentialConfig in Python RPC clients."""

from __future__ import annotations

import os
from typing import Any, Optional


def make_nwchem_params(
    pot_capnp: Any,
    *,
    basis: str = "sto-3g",
    theory: str = "scf",
    scf_type: str = "rhf",
    charge: int = 0,
    multiplicity: int = 1,
    engine_path: str = "",
    nwchem_root: str = "",
):
    """Build an NWChemParams message from keyword arguments."""
    p = pot_capnp.NWChemParams.new_message()
    p.basis = basis
    p.theory = theory
    p.scfType = scf_type
    p.charge = charge
    p.multiplicity = multiplicity
    p.enginePath = engine_path or os.environ.get("RGPOT_NWCHEM_ENGINE", "")
    p.nwchemRoot = nwchem_root or os.environ.get("NWCHEM_TOP", "")
    return p


def make_potential_config_nwchem(pot_capnp: Any, **kwargs):
    """Build PotentialConfig with the nwchem union arm set."""
    cfg = pot_capnp.PotentialConfig.new_message()
    params = make_nwchem_params(pot_capnp, **kwargs)
    cfg.nwchem = params
    return cfg


def make_potential_config_none(pot_capnp: Any):
    """Build PotentialConfig with none arm (no-op configure)."""
    cfg = pot_capnp.PotentialConfig.new_message()
    cfg.none = None
    return cfg


async def configure_nwchem(pot, pot_capnp: Any, **kwargs) -> tuple[bool, str]:
    """Call Potential.configure with NWChemParams; return (ok, message)."""
    cfg = make_potential_config_nwchem(pot_capnp, **kwargs)
    result = await pot.configure(cfg)
    return bool(result.ok), str(result.message)
