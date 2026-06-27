#!/usr/bin/env python3
"""Contract tests for NWChem configure smoke plumbing in rpc_integ.py."""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path

import capnp

SCRIPT_DIR = Path(__file__).resolve().parent
SCHEMA_PATH = SCRIPT_DIR / "../CppCore/rgpot/rpc/Potentials.capnp"
pot_capnp = capnp.load(str(SCHEMA_PATH))

sys.path.insert(0, str(SCRIPT_DIR))
from nwchem_params import make_potential_config_none  # noqa: E402
from rpc_integ import run_nwchem_smoke  # noqa: E402 — uses live server; unit path below


class FakePotential:
    def __init__(self):
        self.configs = []

    async def configure(self, config):
        self.configs.append(config)

        class Result:
            ok = True
            message = "ok"

        return Result()


async def _configure_nwchem_smoke_local(pot, pot_capnp_mod):
    """Mirror run_nwchem_smoke configure sequence without a live server."""
    from nwchem_params import configure_nwchem, make_potential_config_none as mk_none

    cfg_none = mk_none(pot_capnp_mod)
    r0 = await pot.configure(cfg_none)
    assert r0.ok
    ok, msg = await configure_nwchem(
        pot, pot_capnp_mod, basis="sto-3g", theory="scf", scf_type="rhf"
    )
    assert ok is True
    return True


def test_configure_nwchem_smoke_sequence() -> None:
    pot = FakePotential()
    ok = asyncio.run(_configure_nwchem_smoke_local(pot, pot_capnp))
    assert ok is True
    assert [cfg.which() for cfg in pot.configs] == ["none", "nwchem"]
    assert pot.configs[1].nwchem.basis == "sto-3g"
    assert pot.configs[1].nwchem.theory == "scf"
    assert pot.configs[1].nwchem.scfType == "rhf"


if __name__ == "__main__":
    test_configure_nwchem_smoke_sequence()
    print("test_rpc_integ_nwchem: all ok")
