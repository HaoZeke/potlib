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
    assert [cfg.which() for cfg in pot.configs] == ["none", "cpmd"]
    assert pot.configs[1].cpmd.functional == "BLYP"
    assert pot.configs[1].cpmd.task == "gradient"


if __name__ == "__main__":
    test_configure_cpmd_smoke()
