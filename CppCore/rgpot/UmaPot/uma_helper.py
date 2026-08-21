#!/usr/bin/env python3
"""Persistent UMA / OMol energy-force helper for rgpot UmaPot.

Line-JSON protocol on stdin/stdout. One process loads the fairchem
predictor once; each subsequent eval is one JSON object.

Request:
  {"op":"eval","pos":[...],"atmnrs":[...],"box":[9],"charge":0,"spin":1}
  {"op":"shutdown"}

Response:
  {"ok":true,"energy":...,"forces":[...]}
  {"ok":false,"error":"..."}

Fake mode (``--fake`` or ``RGPOT_UMA_FAKE=1``) returns a harmonic well so
C++ tests do not need fairchem or a HuggingFace token.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any


def _json_line(obj: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def _harmonic(pos: list[float]) -> tuple[float, list[float]]:
    energy = 0.5 * sum(x * x for x in pos)
    forces = [-x for x in pos]
    return energy, forces


def _load_fairchem(model: str, task_name: str, device: str):
    from ase import Atoms
    from fairchem.core import FAIRChemCalculator, pretrained_mlip

    predictor = pretrained_mlip.get_predict_unit(model, device=device)
    calc = FAIRChemCalculator(predictor, task_name=task_name)
    return Atoms, calc


def _eval_fairchem(Atoms, calc, payload: dict[str, Any]) -> tuple[float, list[float]]:
    pos = payload["pos"]
    atmnrs = payload["atmnrs"]
    box = payload["box"]
    n = len(atmnrs)
    if len(pos) != 3 * n:
        raise ValueError(f"pos length {len(pos)} != 3 * natoms {n}")
    if len(box) != 9:
        raise ValueError(f"box length {len(box)} != 9")
    positions = [(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]) for i in range(n)]
    cell = [
        (box[0], box[1], box[2]),
        (box[3], box[4], box[5]),
        (box[6], box[7], box[8]),
    ]
    atoms = Atoms(numbers=list(atmnrs), positions=positions, cell=cell, pbc=True)
    atoms.info.update(
        {
            "charge": int(payload.get("charge", 0)),
            "spin": int(payload.get("spin", 1)),
        }
    )
    atoms.calc = calc
    energy = float(atoms.get_potential_energy())
    forces = atoms.get_forces().reshape(-1).tolist()
    return energy, forces


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", default="uma-s-1p1")
    parser.add_argument("--task-name", default="omol")
    parser.add_argument("--device", default="cpu")
    parser.add_argument(
        "--fake",
        action="store_true",
        help="Harmonic well; no fairchem import",
    )
    args = parser.parse_args(argv)

    fake = args.fake or os.environ.get("RGPOT_UMA_FAKE", "") in ("1", "true", "TRUE")
    backend = "fake"
    atoms_cls = None
    calc = None
    if not fake:
        try:
            atoms_cls, calc = _load_fairchem(args.model, args.task_name, args.device)
            backend = "fairchem"
        except Exception as exc:
            _json_line({"ok": False, "error": f"fairchem load failed: {exc}"})
            return 1

    _json_line({"ok": True, "ready": True, "backend": backend})

    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError as exc:
            _json_line({"ok": False, "error": f"invalid json: {exc}"})
            continue
        op = msg.get("op", "eval")
        if op == "shutdown":
            _json_line({"ok": True, "bye": True})
            return 0
        if op != "eval":
            _json_line({"ok": False, "error": f"unknown op {op!r}"})
            continue
        try:
            if fake:
                energy, forces = _harmonic(list(msg["pos"]))
            else:
                energy, forces = _eval_fairchem(atoms_cls, calc, msg)
            _json_line({"ok": True, "energy": energy, "forces": forces})
        except Exception as exc:
            _json_line({"ok": False, "error": str(exc)})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
