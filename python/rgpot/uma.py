"""Evaluate UMA / OMol through the shipped uma_helper.py protocol."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Iterable


def default_uma_helper_path() -> Path | None:
    here = Path(__file__).resolve().parent
    candidates = [
        here / "uma_helper.py",
        here.parent.parent / "CppCore" / "rgpot" / "UmaPot" / "uma_helper.py",
    ]
    env = os.environ.get("RGPOT_UMA_HELPER")
    if env:
        candidates.insert(0, Path(env))
    for path in candidates:
        if path.is_file():
            return path
    return None


def evaluate_uma(
    positions: Iterable[Iterable[float]],
    atom_types: Iterable[int],
    box: Iterable[Iterable[float]],
    *,
    model: str = "uma-s-1p1",
    task_name: str = "omol",
    device: str = "cpu",
    charge: int = 0,
    spin: int = 1,
    helper: str | os.PathLike[str] | None = None,
    python: str | None = None,
    fake: bool | None = None,
) -> tuple[float, list[list[float]]]:
    """Return (energy, forces) from one helper lifetime.

    ``fake=True`` or ``RGPOT_UMA_FAKE=1`` uses the harmonic well and does
    not import fairchem.
    """
    helper_path = Path(helper) if helper is not None else default_uma_helper_path()
    if helper_path is None:
        raise FileNotFoundError(
            "uma_helper.py not found; set RGPOT_UMA_HELPER or pass helper="
        )
    env = os.environ.copy()
    if fake is True:
        env["RGPOT_UMA_FAKE"] = "1"
    cmd = [
        python or env.get("RGPOT_UMA_PYTHON", sys.executable),
        str(helper_path),
        "--model",
        model,
        "--task-name",
        task_name,
        "--device",
        device,
    ]
    pos = [float(x) for row in positions for x in row]
    box_flat = [float(x) for row in box for x in row]
    atmnrs = [int(z) for z in atom_types]
    payload = {
        "op": "eval",
        "pos": pos,
        "atmnrs": atmnrs,
        "box": box_flat,
        "charge": int(charge),
        "spin": int(spin),
    }
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    assert proc.stdin is not None and proc.stdout is not None
    ready = proc.stdout.readline()
    handshake = json.loads(ready)
    if not handshake.get("ok"):
        proc.kill()
        raise RuntimeError(handshake.get("error", ready))
    proc.stdin.write(json.dumps(payload) + "\n")
    proc.stdin.write(json.dumps({"op": "shutdown"}) + "\n")
    proc.stdin.flush()
    reply = json.loads(proc.stdout.readline())
    proc.communicate(timeout=30)
    if not reply.get("ok"):
        raise RuntimeError(reply.get("error", "uma helper eval failed"))
    forces = reply["forces"]
    n = len(atmnrs)
    shaped = [forces[3 * i : 3 * i + 3] for i in range(n)]
    return float(reply["energy"]), shaped
