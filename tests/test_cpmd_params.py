#!/usr/bin/env python3
"""Contract tests for CPMD PotentialConfig helpers."""

from __future__ import annotations

import asyncio
import os
import sys
from pathlib import Path

import capnp

SCRIPT_DIR = Path(__file__).resolve().parent
SCHEMA_PATH = SCRIPT_DIR / "../CppCore/rgpot/rpc/Potentials.capnp"
pot_capnp = capnp.load(str(SCHEMA_PATH))

try:
    from cpmd_params import (
        configure_cpmd,
        make_cpmd_params,
        make_potential_config_cpmd,
    )
except ImportError:
    sys.path.insert(0, str(SCRIPT_DIR))
    from cpmd_params import (
        configure_cpmd,
        make_cpmd_params,
        make_potential_config_cpmd,
    )


class FakePotential:
    def __init__(self):
        self.config = None

    async def configure(self, config):
        self.config = config

        class Result:
            ok = True
            message = "configured"

        return Result()


def test_make_cpmd_params() -> None:
    os.environ["CPMD_ROOT"] = "/env/cpmd"
    os.environ["RGPOT_CPMD_ENGINE"] = "/env/libcpmdc.so"
    params = make_cpmd_params(
        pot_capnp,
        functional="PBE",
        cut_off_ry=90.0,
        charge=-1,
        multiplicity=2,
        task="energy",
        title="water",
        memory_mb=512,
        scratch_dir="/tmp/cpmd-scratch",
        permanent_dir="/tmp/cpmd-perm",
        input_blocks=["&CPMD\n  OPTIMIZE WAVEFUNCTION\n&END"],
        system_cell=[9.0, 0.0, 0.0, 0.0, 9.5, 0.0, 0.0, 0.0, 10.0],
        pseudopotentials=[{"element": "O", "path": "O_BLYP.psp", "lmax": 2}],
        set_directives=[{"key": "SYSTEM.POISSON SOLVER", "value": "HOCKNEY"}],
    )

    assert params.functional == "PBE"
    assert params.cutOffRy == 90.0
    assert params.charge == -1
    assert params.multiplicity == 2
    assert params.task == "energy"
    assert params.title == "water"
    assert params.memoryMb == 512
    assert params.scratchDir == "/tmp/cpmd-scratch"
    assert params.permanentDir == "/tmp/cpmd-perm"
    assert params.cpmdRoot == "/env/cpmd"
    assert params.enginePath == "/env/libcpmdc.so"
    assert list(params.inputBlocks) == ["&CPMD\n  OPTIMIZE WAVEFUNCTION\n&END"]

    sections = params.inputSections
    assert len(sections) == 3
    assert sections[0].which() == "system"
    assert list(sections[0].system.cell) == [
        9.0,
        0.0,
        0.0,
        0.0,
        9.5,
        0.0,
        0.0,
        0.0,
        10.0,
    ]
    assert sections[0].system.cutOffRy == 90.0
    assert sections[1].which() == "atoms"
    assert sections[1].atoms.pseudopotentials[0].element == "O"
    assert sections[1].atoms.pseudopotentials[0].path == "O_BLYP.psp"
    assert sections[1].atoms.pseudopotentials[0].lmax == 2
    assert sections[2].which() == "set"
    assert sections[2].set.key == "SYSTEM.POISSON SOLVER"
    assert sections[2].set.value == "HOCKNEY"


def test_make_potential_config_cpmd() -> None:
    cfg = make_potential_config_cpmd(pot_capnp, functional="BLYP")
    assert cfg.which() == "cpmd"
    assert cfg.cpmd.functional == "BLYP"


def test_configure_cpmd() -> None:
    pot = FakePotential()
    ok, message = asyncio.run(configure_cpmd(pot, pot_capnp, functional="PBE"))
    assert ok is True
    assert message == "configured"
    assert pot.config.which() == "cpmd"
    assert pot.config.cpmd.functional == "PBE"


if __name__ == "__main__":
    test_make_cpmd_params()
    test_make_potential_config_cpmd()
    test_configure_cpmd()
