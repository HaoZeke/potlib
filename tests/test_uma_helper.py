"""uma_helper.py line-JSON protocol, no fairchem and no librgpot required."""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

HELPER = (
    Path(__file__).resolve().parents[1]
    / "CppCore"
    / "rgpot"
    / "UmaPot"
    / "uma_helper.py"
)


def test_fake_harmonic_well():
    env = os.environ.copy()
    env["RGPOT_UMA_FAKE"] = "1"
    proc = subprocess.Popen(
        [sys.executable, str(HELPER), "--model", "uma-s-1p1", "--task-name", "omol"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    assert proc.stdin is not None and proc.stdout is not None
    handshake = json.loads(proc.stdout.readline())
    assert handshake["ok"] is True
    assert handshake["backend"] == "fake"
    payload = {
        "op": "eval",
        "pos": [1.0, 0.0, 0.0, 0.0, 2.0, 0.0],
        "atmnrs": [1, 6],
        "box": [20.0, 0.0, 0.0, 0.0, 20.0, 0.0, 0.0, 0.0, 20.0],
        "charge": 0,
        "spin": 1,
    }
    proc.stdin.write(json.dumps(payload) + "\n")
    proc.stdin.write(json.dumps({"op": "shutdown"}) + "\n")
    proc.stdin.flush()
    reply = json.loads(proc.stdout.readline())
    proc.communicate(timeout=10)
    assert reply["ok"] is True
    assert abs(reply["energy"] - 2.5) < 1e-12
    assert abs(reply["forces"][0] + 1.0) < 1e-12
    assert abs(reply["forces"][4] + 2.0) < 1e-12
