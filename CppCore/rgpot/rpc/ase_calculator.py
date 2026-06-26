#!/usr/bin/env python3
"""ASE calculator backed by an rgpot Cap'n Proto potential server (``potserv``).

This is the frictionless Python entry point to rgpot potentials: construct a
calculator, attach it to an :class:`ase.Atoms`, and run any ASE optimizer or
dynamics. Energies and forces are evaluated by an rgpot ``potserv`` process over
the language-neutral Cap'n Proto RPC, so the same potential is reachable from
ASE, anneal, Julia, or any other Cap'n Proto client.

Two usage modes:

* *Managed server* (zero setup) -- give the calculator the ``potserv`` binary
  and a potential name; it launches the server on a free port, connects, and
  shuts it down on :meth:`close` / context-manager exit::

      with RgpotCalculator.spawn(server_bin="...potserv", potential="CuH2") as calc:
          atoms.calc = calc
          LBFGS(atoms).run(fmax=0.02)

* *Attach to a running server* -- connect to an existing ``potserv``::

      calc = RgpotCalculator(host="localhost", port=12345)

Units follow the Cap'n Proto ``ForceInput`` defaults (Angstrom / eV), which are
exactly ASE's units, so no conversion is applied.
"""

from __future__ import annotations

import asyncio
import os
import socket
import subprocess
from pathlib import Path

import capnp
import numpy as np
from ase.calculators.calculator import Calculator, all_changes

_SCHEMA_PATH = Path(__file__).resolve().parent / "Potentials.capnp"


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("localhost", 0))
        return s.getsockname()[1]


class RgpotCalculator(Calculator):
    """ASE calculator that evaluates energy/forces through an rgpot ``potserv``.

    Parameters
    ----------
    host, port:
        Address of a running ``potserv`` instance.
    server_proc:
        Optional managed :class:`subprocess.Popen` whose lifetime this
        calculator owns (killed on :meth:`close`). Prefer :meth:`spawn`.
    """

    implemented_properties = ["energy", "forces"]

    def __init__(self, host="localhost", port=12345, *, server_proc=None, **kwargs):
        super().__init__(**kwargs)
        self._host = host
        self._port = int(port)
        self._server_proc = server_proc

        self._schema = capnp.load(str(_SCHEMA_PATH))
        # One persistent kj/asyncio loop + connection, driven synchronously in
        # the calling thread. No background thread and no cross-thread handoff:
        # each energy/force evaluation is a single in-thread run_until_complete
        # over a live RPC channel, which is what keeps the per-eval cost at the
        # bare RPC round-trip for tight optimizer loops.
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._kj = capnp.kj_loop()
        self._closed = False
        self._pot = None
        self._loop.run_until_complete(self._kj.__aenter__())
        try:
            self._loop.run_until_complete(self._connect())
        except BaseException:
            self.close()
            raise

    # -- server lifecycle ---------------------------------------------------

    @classmethod
    def spawn(cls, server_bin, potential="CuH2", port=None, *, startup_timeout=30.0, **kwargs):
        """Launch a managed ``potserv`` and return a calculator bound to it.

        ``server_bin`` may be a path or come from the ``RGPOT_POTSERV_BIN``
        environment variable when passed as ``None``.
        """
        if server_bin is None:
            server_bin = os.environ.get("RGPOT_POTSERV_BIN")
        if not server_bin or not Path(server_bin).exists():
            raise FileNotFoundError(
                f"potserv binary not found: {server_bin!r} "
                "(set the path or RGPOT_POTSERV_BIN)"
            )
        port = port or _free_port()
        proc = subprocess.Popen(
            [str(server_bin), str(port), potential],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            return cls(host="localhost", port=port, server_proc=proc, **kwargs)
        except Exception:
            proc.kill()
            proc.wait()
            raise

    def close(self):
        """Disconnect and stop a managed server, if any."""
        if not self._closed:
            self._closed = True
            try:
                self._loop.run_until_complete(self._kj.__aexit__(None, None, None))
            except Exception:
                pass
            self._loop.close()
        if self._server_proc is not None:
            self._server_proc.kill()
            self._server_proc.wait()
            self._server_proc = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- persistent connection driven synchronously ------------------------

    async def _connect(self):
        last_err = None
        for _ in range(120):  # tolerate managed-server startup latency
            try:
                connection = await capnp.AsyncIoStream.create_connection(
                    host=self._host, port=self._port
                )
                client = capnp.TwoPartyClient(connection)
                self._pot = client.bootstrap().cast_as(self._schema.Potential)
                return
            except OSError as exc:
                last_err = exc
                await asyncio.sleep(0.25)
        raise RuntimeError(
            f"RgpotCalculator: could not connect to {self._host}:{self._port}: {last_err}"
        )

    async def _calculate_async(self, positions, numbers, box):
        # Whole-list assignment (one capnp call per field) is markedly faster on
        # the hot path than element-by-element writes.
        fip = self._schema.ForceInput.new_message()
        fip.pos = np.asarray(positions, dtype=float).ravel().tolist()
        fip.atmnrs = [int(z) for z in numbers]
        fip.box = np.asarray(box, dtype=float).ravel()[:9].tolist()
        result = await self._pot.calculate(fip)
        energy = float(result.result.energy)
        forces = np.array(result.result.forces, dtype=float).reshape(-1, 3)
        return energy, forces

    # -- ASE Calculator interface -------------------------------------------

    def calculate(self, atoms=None, properties=("energy",), system_changes=all_changes):
        super().calculate(atoms, properties, system_changes)
        box = atoms.cell.array if atoms.cell.rank == 3 else np.eye(3) * 100.0
        energy, forces = self._loop.run_until_complete(
            self._calculate_async(
                atoms.get_positions(), atoms.get_atomic_numbers(), box
            )
        )
        self.results["energy"] = energy
        self.results["free_energy"] = energy
        self.results["forces"] = forces
