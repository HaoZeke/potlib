#!/usr/bin/env python3
"""Contract test for CPMD configure smoke plumbing in rpc_integ.py."""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path

import capnp

SCRIPT_DIR = Path(__file__).resolve().parent
SCHEMA_PATH = SCRIPT_DIR / "../CppCore/rgpot/rpc/Potentials.capnp"
pot_capnp = capnp.load(str(SCHEMA_PATH))

sys.path.insert(0, str(SCRIPT_DIR))
from rpc_integ import configure_cpmd_smoke  # noqa: E402


class FakePotential:
    def __init__(self):
        self.configs = []

    async def configure(self, config):
        self.configs.append(config)

        class Result:
            ok = len(self.configs) == 1
            message = "no-op" if ok else "engine not loaded"

        return Result()


def test_configure_cpmd_smoke() -> None:
    pot = FakePotential()
    ok = asyncio.run(configure_cpmd_smoke(pot, pot_capnp))
    assert ok is True
    assert [cfg.which() for cfg in pot.configs] == ["none", "cpmd", "cpmd"]
    assert pot.configs[1].cpmd.functional == "BLYP"
    assert pot.configs[1].cpmd.task == "gradient"
    assert pot.configs[2].cpmd.functional == "PBE0"
    assert pot.configs[2].cpmd.task == "gradient"
    cpmd_section = pot.configs[2].cpmd.inputSections[1].cpmd
    assert cpmd_section.optimizeGeometry is True
    assert cpmd_section.molecularDynamicsCp is True
    assert cpmd_section.molecularDynamicsBo is True
    assert cpmd_section.molecularDynamicsEh is True
    assert cpmd_section.molecularDynamicsPt is True
    assert cpmd_section.molecularDynamicsClassical is True
    assert cpmd_section.molecularDynamicsFile == "TRAJECTORY.in"
    assert cpmd_section.convergenceGeometry == 1.0e-4
    assert cpmd_section.maxIter == 12
    assert cpmd_section.electronMass == 450.0
    assert cpmd_section.nose is True
    assert cpmd_section.noseIons is True
    assert cpmd_section.noseElectrons is True
    assert cpmd_section.berendsen == "300 100"
    assert cpmd_section.langevin is True
    assert cpmd_section.annealing == "IONS 300 50"
    assert cpmd_section.quench is True
    assert cpmd_section.rattle is True
    assert cpmd_section.shake is True
    assert cpmd_section.constraint == "FIX COM"
    assert cpmd_section.trotter == "8"
    assert cpmd_section.restart is True
    assert cpmd_section.printOptions == "FORCES ON"
    assert cpmd_section.storeOptions == "WAVEFUNCTION"
    assert cpmd_section.centerMoleculeOff is True
    assert cpmd_section.centerMoleculeOn is True
    assert cpmd_section.diis is True
    assert cpmd_section.odiis is True
    assert cpmd_section.pcg is True
    assert cpmd_section.diagonalization is True
    assert cpmd_section.freeEnergy is True
    assert cpmd_section.interface is True
    assert cpmd_section.qmmm is True
    assert cpmd_section.bicanonicalEnsemble is True
    assert cpmd_section.cdft is True
    assert cpmd_section.properties is True
    dft_section = pot.configs[2].cpmd.inputSections[2].dft
    assert dft_section.gcCutoff == 1.0e-8
    assert dft_section.xcDriver == "LIBXC"
    assert dft_section.libxc == "GGA_X_PBE GGA_C_PBE"
    assert dft_section.lrKernel == "PBE"
    assert dft_section.refunct == "PBE"
    assert dft_section.mtsHighFunc == "PBE0"
    assert dft_section.mtsLowFunc == "PBE"
    assert dft_section.hfx is True
    assert dft_section.hfxScreening == "0.2"
    assert dft_section.hubbard == "U 1 4.0"
    assert dft_section.alpha == 0.25
    assert dft_section.beta == 0.75
    assert dft_section.oldCode is True
    assert dft_section.newCode is True
    assert dft_section.correlation == "LYP"
    assert dft_section.exchange == "B88"
    assert dft_section.becke88 is True


if __name__ == "__main__":
    test_configure_cpmd_smoke()
