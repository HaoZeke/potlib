#!/usr/bin/env python3
"""Freeze UMA-omol, export a tensor-only module, AOTI-package it.

Neighbor lists stay outside the graph (vesin). The exported module consumes
precomputed edge_index / cell_offsets, matching eSCNMDBackbone with
otf_graph=False. merge_mole stays on, so one .pt2 covers one composition
(z_set, charge, spin).

Stages:
  1. ASE FAIRChemCalculator reference on the chosen atoms
  2. Eager tensor wrapper + vesin vs that reference
  3. torch.export
  4. AOTInductor .pt2
  5. Load .pt2 in-process and compare again
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from ase import Atoms


HCN = Atoms(
    numbers=[6, 7, 1],
    positions=[
        [12.49734736216627162, 12.49892801474515913, 12.54059929828148512],
        [12.50115413363106498, 12.50036504272228832, 11.38209979880783251],
        [12.50149850420264563, 12.50069809648255514, 13.61514544631068446],
    ],
    cell=[[25.0, 0.0, 0.0], [0.0, 25.0, 0.0], [0.0, 0.0, 25.0]],
    pbc=True,
)
HCN.info.update({"charge": 0, "spin": 1})


def z_set_of(atoms: Atoms) -> list[int]:
    return sorted({int(z) for z in atoms.get_atomic_numbers()})


def default_label(atoms_path: str | None) -> str:
    if not atoms_path:
        return "hcn"
    path = Path(atoms_path)
    if path.name.lower() in {
        "reactant.con",
        "product.con",
        "reactant.xyz",
        "product.xyz",
    }:
        return path.parent.name
    return path.stem


def _cellpar_to_cell(lengths, angles):
    from ase.geometry import cellpar_to_cell

    return cellpar_to_cell(list(lengths) + list(angles))


def _read_eon_con(path: Path) -> Atoms:
    """Read an eOn .con (ASE has no built-in eOn reader)."""
    lines = Path(path).read_text().splitlines()
    if len(lines) < 9:
        raise ValueError(f"{path}: too short for eOn .con")
    lengths = [float(x) for x in lines[2].split()[:3]]
    angles = [float(x) for x in lines[3].split()[:3]]
    ntypes = int(lines[6].split()[0])
    counts = [int(x) for x in lines[7].split()[:ntypes]]
    if len(counts) < ntypes:
        raise ValueError(f"{path}: expected {ntypes} type counts")
    symbols: list[str] = []
    positions: list[list[float]] = []
    i = 9
    for n_at in counts:
        if i >= len(lines):
            raise ValueError(f"{path}: truncated type header")
        sym = lines[i].strip().split()[0]
        i += 1
        if i < len(lines) and lines[i].lower().startswith("coordinates"):
            i += 1
        for _ in range(n_at):
            if i >= len(lines):
                raise ValueError(f"{path}: truncated coordinates")
            parts = lines[i].split()
            positions.append([float(parts[0]), float(parts[1]), float(parts[2])])
            symbols.append(sym)
            i += 1
    return Atoms(
        symbols=symbols,
        positions=positions,
        cell=_cellpar_to_cell(lengths, angles),
        pbc=True,
    )


def _ensure_box(atoms: Atoms) -> Atoms:
    cell = atoms.get_cell()
    if cell is None or float(cell.volume) < 1e-8:
        atoms.set_cell([[25.0, 0.0, 0.0], [0.0, 25.0, 0.0], [0.0, 0.0, 25.0]])
        atoms.set_pbc(True)
    elif not bool(atoms.pbc.any()):
        atoms.set_pbc(True)
    return atoms


def load_atoms(path: str | os.PathLike | None) -> Atoms:
    """Load .xyz/.con via ASE, then an eOn .con fallback. Default is Baker HCN."""
    if path is None:
        return HCN.copy()
    path = Path(path)
    if not path.is_file():
        raise SystemExit(f"--atoms not found: {path}")
    if path.suffix.lower() == ".con":
        try:
            return _ensure_box(_read_eon_con(path))
        except Exception:
            from ase.io import read

            return _ensure_box(read(str(path)))
    from ase.io import read

    return _ensure_box(read(str(path)))


def _vesin_edges(
    pos: np.ndarray,
    cell: np.ndarray,
    cutoff: float,
    pbc=(True, True, True),
    max_neighbors: int | None = None,
):
    """Fairchem convention: edge_index = [neighbor, center], drop self-loops."""
    try:
        from vesin import NeighborList
    except ImportError as exc:
        raise SystemExit("vesin is required for export_uma_aoti") from exc

    nl = NeighborList(cutoff=float(cutoff), full_list=True)
    i, j, S = nl.compute(points=pos, box=cell, periodic=pbc, quantities="ijS")
    # vesin (i, j) is a pair; treat i as center, j as neighbor (fairchem n,c)
    n_index = np.asarray(j, dtype=np.int64)
    c_index = np.asarray(i, dtype=np.int64)
    shifts = np.asarray(S, dtype=np.float64)
    dvec = pos[n_index] - pos[c_index] + shifts @ cell
    dist = np.linalg.norm(dvec, axis=1)
    keep = dist >= 1e-8
    n_index, c_index, shifts, dist = n_index[keep], c_index[keep], shifts[keep], dist[keep]
    if max_neighbors is not None and max_neighbors > 0:
        keep_idx = []
        for center in np.unique(c_index):
            idx = np.where(c_index == center)[0]
            order = np.argsort(dist[idx])[: int(max_neighbors)]
            keep_idx.append(idx[order])
        if keep_idx:
            sel = np.concatenate(keep_idx)
            n_index, c_index, shifts = n_index[sel], c_index[sel], shifts[sel]
    edge_index = np.stack([n_index, c_index], axis=0)
    return edge_index, shifts


def vesin_atomic_data(
    atoms: Atoms,
    cutoff: float,
    dtype,
    device,
    max_neighbors: int | None,
    *,
    task_name: str = "omol",
    sid: str = "hcn",
):
    """Official AtomicData carrying vesin edges (otf_graph=False path)."""
    from fairchem.core.datasets.atomic_data import AtomicData

    pos_np = np.asarray(atoms.get_positions(), dtype=np.float64)
    cell_np = np.asarray(atoms.get_cell(), dtype=np.float64)
    edge_index, shifts = _vesin_edges(pos_np, cell_np, cutoff, max_neighbors=max_neighbors)
    pos = torch.tensor(pos_np, dtype=dtype)
    data = AtomicData(
        pos=pos,
        atomic_numbers=torch.tensor(atoms.get_atomic_numbers(), dtype=torch.long),
        cell=torch.tensor(cell_np, dtype=dtype).unsqueeze(0),
        pbc=torch.tensor([[True, True, True]]),
        natoms=torch.tensor([pos.shape[0]], dtype=torch.long),
        edge_index=torch.tensor(edge_index, dtype=torch.long),
        cell_offsets=torch.tensor(shifts, dtype=dtype),
        nedges=torch.tensor([edge_index.shape[1]], dtype=torch.long),
        charge=torch.tensor([int(atoms.info.get("charge", 0))], dtype=torch.long),
        spin=torch.tensor([int(atoms.info.get("spin", 1))], dtype=torch.long),
        fixed=torch.zeros(pos.shape[0], dtype=torch.long),
        tags=torch.zeros(pos.shape[0], dtype=torch.long),
        dataset=[task_name],
        sid=[sid],
    )
    return data.to(device)


class BakedDatasetEmbedding(nn.Module):
    """Single-system omol embedding. No Python string list."""

    def __init__(self, emb: torch.Tensor):
        super().__init__()
        self.register_buffer("emb", emb.detach().contiguous())

    def forward(self, dataset_list=None):
        return self.emb


class TensorData(dict):
    """AtomicData-shaped mapping. fairchem uses both data[k] and data.k."""

    def get(self, key, default=None, **kwargs):
        if "default" in kwargs:
            default = kwargs["default"]
        return super().get(key, default)

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        try:
            return self[name]
        except KeyError as exc:
            raise AttributeError(name) from exc

    def __setattr__(self, name, value):
        if name.startswith("_"):
            super().__setattr__(name, value)
        else:
            self[name] = value

    def __len__(self):
        natoms = self.get("natoms")
        if natoms is None:
            return super().__len__()
        return int(natoms.numel())


def _lookup_task(out, task_name: str, prop: str):
    """Hydra output is either {task: {prop: t}} or a flattened pass-through."""
    if task_name in out:
        payload = out[task_name]
        if isinstance(payload, dict) and prop in payload:
            return payload[prop]
        if torch.is_tensor(payload):
            return payload
    if isinstance(out, dict):
        for key, payload in out.items():
            if not isinstance(payload, dict):
                continue
            if key == task_name and prop in payload:
                return payload[prop]
            if prop in payload and (task_name in key or key == prop):
                return payload[prop]
            nested = _lookup_task(payload, task_name, prop)
            if nested is not None:
                return nested
    return None


class TensorUma(nn.Module):
    """Tensor-only UMA forward. Graph is an input, not computed here.

    Calls the merged EFS head directly (no MoE dataset routing) and then
    matches MLIPPredictUnit._process_outputs: denorm, undo elemental refs.
    """

    def __init__(self, hydra_model: nn.Module, pred_unit=None, task_name: str = "omol"):
        super().__init__()
        self.task_name = task_name
        self.energy_task = f"{task_name}_energy"
        self.force_task = f"{task_name}_forces"
        e_mean = torch.zeros((), dtype=torch.float64)
        e_rmsd = torch.ones((), dtype=torch.float64)
        f_mean = torch.zeros((), dtype=torch.float64)
        f_rmsd = torch.ones((), dtype=torch.float64)
        elem_refs = torch.zeros(128, dtype=torch.float64)
        if pred_unit is not None:
            tasks = pred_unit.dataset_to_tasks[task_name]
            for task in tasks:
                if task.property == "energy":
                    self.energy_task = task.name
                    e_mean = task.normalizer.mean.detach().reshape(()).to(torch.float64)
                    e_rmsd = task.normalizer.rmsd.detach().reshape(()).to(torch.float64)
                    if task.element_references is not None:
                        elem_refs = (
                            task.element_references.element_references.detach().to(
                                torch.float64
                            )
                        )
                elif task.property == "forces":
                    self.force_task = task.name
                    f_mean = task.normalizer.mean.detach().reshape(()).to(torch.float64)
                    f_rmsd = task.normalizer.rmsd.detach().reshape(()).to(torch.float64)
        self.register_buffer("e_mean", e_mean)
        self.register_buffer("e_rmsd", e_rmsd)
        self.register_buffer("f_mean", f_mean)
        self.register_buffer("f_rmsd", f_rmsd)
        self.register_buffer("elem_refs", elem_refs)
        hydra = hydra_model.module if hasattr(hydra_model, "module") else hydra_model
        backbone = hydra.backbone
        if hasattr(backbone, "dataset_embedding"):
            de = backbone.dataset_embedding
            idx = torch.tensor(0, dtype=torch.long)
            with torch.no_grad():
                baked = de.dataset_emb_dict[task_name](
                    idx.to(next(de.parameters()).device)
                )
            if baked.dim() == 1:
                baked = baked.unsqueeze(0)
            backbone.dataset_embedding = BakedDatasetEmbedding(baked)
        efs = None
        for _name, head in hydra.output_heads.items():
            inner = getattr(head, "head", head)
            if hasattr(inner, "energy_block"):
                efs = inner
                break
        if efs is None:
            raise RuntimeError(
                f"no EFS head in {list(hydra.output_heads.keys())}"
            )
        self.backbone = backbone
        self.efs = efs

    def forward(
        self,
        pos: torch.Tensor,
        atomic_numbers: torch.Tensor,
        cell: torch.Tensor,
        pbc: torch.Tensor,
        edge_index: torch.Tensor,
        cell_offsets: torch.Tensor,
        charge: torch.Tensor,
        spin: torch.Tensor,
        batch: torch.Tensor,
        natoms: torch.Tensor,
    ):
        pos = pos.clone().requires_grad_(True)
        nedges = edge_index.new_zeros(1)
        nedges[0] = edge_index.shape[1]
        z = atomic_numbers.long()
        b = batch.long()
        data = TensorData(
            pos=pos,
            atomic_numbers=z,
            atomic_numbers_full=z,
            cell=cell,
            pbc=pbc,
            edge_index=edge_index.long(),
            cell_offsets=cell_offsets,
            nedges=nedges,
            charge=charge.long(),
            spin=spin.long(),
            batch=b,
            batch_full=b,
            natoms=natoms.long(),
            fixed=torch.zeros_like(z),
            tags=torch.zeros_like(z),
            dataset=[self.task_name],
            sid=[""],
        )
        emb = self.backbone(data)
        # Bypass MLP_EFS_Head: compute_energy is @torch.compiler.disable
        # (index_add + float64), which export lifts as a fake constant.
        # Single-system sum is the same reduction and is traceable.
        node_emb = emb["node_embedding"]
        scalar = node_emb.narrow(1, 0, 1).squeeze(1)
        node_e = self.efs.energy_block(scalar).reshape(-1)
        energy_raw = node_e.to(torch.float64).sum()
        energy = (
            energy_raw * self.e_rmsd
            + self.e_mean
            + self.elem_refs.to(device=z.device)[z].sum()
        )
        (g,) = torch.autograd.grad(energy_raw, pos, create_graph=False)
        forces = (-g).to(torch.float64) * self.f_rmsd + self.f_mean
        return energy, forces


def _settings():
    from fairchem.core.units.mlip_unit.api.inference import InferenceSettings

    return InferenceSettings(
        tf32=False,
        activation_checkpointing=False,
        merge_mole=True,
        compile=False,
        external_graph_gen=True,
        internal_graph_gen_version=2,
        execution_mode="general",
        auto_add_default_untrained_tasks=True,
    )


def load_predictor(name: str, device: str):
    from fairchem.core import pretrained_mlip

    return pretrained_mlip.get_predict_unit(
        name, device=device, inference_settings=_settings()
    )


def ase_reference(atoms: Atoms, model: str, device: str, task_name: str = "omol"):
    """Official ASE path: internal graph gen, no pymatgen."""
    from fairchem.core import FAIRChemCalculator, pretrained_mlip
    from fairchem.core.units.mlip_unit.api.inference import InferenceSettings

    settings = InferenceSettings(
        tf32=False,
        activation_checkpointing=False,
        merge_mole=True,
        compile=False,
        external_graph_gen=False,
        execution_mode="general",
    )
    pred = pretrained_mlip.get_predict_unit(
        model, device=device, inference_settings=settings
    )
    a = atoms.copy()
    a.calc = FAIRChemCalculator(pred, task_name=task_name)
    e = float(a.get_potential_energy())
    f = np.asarray(a.get_forces(), dtype=np.float64)
    return e, f, pred


def pack_example(atoms: Atoms, cutoff: float, device, dtype, max_neighbors: int | None = None):
    pos_np = np.asarray(atoms.get_positions(), dtype=np.float64)
    cell_np = np.asarray(atoms.get_cell(), dtype=np.float64)
    edge_index, shifts = _vesin_edges(pos_np, cell_np, cutoff, max_neighbors=max_neighbors)
    pos = torch.tensor(pos_np, dtype=dtype, device=device)
    atomic_numbers = torch.tensor(atoms.get_atomic_numbers(), dtype=torch.long, device=device)
    cell = torch.tensor(cell_np, dtype=dtype, device=device).unsqueeze(0)
    pbc = torch.tensor([[True, True, True]], device=device)
    ei = torch.tensor(edge_index, dtype=torch.long, device=device)
    off = torch.tensor(shifts, dtype=dtype, device=device)
    charge = torch.tensor([int(atoms.info.get("charge", 0))], dtype=torch.long, device=device)
    spin = torch.tensor([int(atoms.info.get("spin", 1))], dtype=torch.long, device=device)
    batch = torch.zeros(pos.shape[0], dtype=torch.long, device=device)
    natoms = torch.tensor([pos.shape[0]], dtype=torch.long, device=device)
    return (pos, atomic_numbers, cell, pbc, ei, off, charge, spin, batch, natoms)


def compare(tag: str, e, f, e_ref, f_ref, e_tol=1e-4, f_tol=1e-4) -> bool:
    de = float(e) - float(e_ref)
    df = np.max(np.abs(np.asarray(f, dtype=np.float64) - f_ref))
    print(f"{tag}: E={float(e):.17g}  dE={de:.3e}  max|dF|={df:.3e}", flush=True)
    return abs(de) <= e_tol and df <= f_tol


def patch_export_ops():
    """Replace compiler-disabled custom autograd with plain torch ops."""
    import fairchem.core.models.uma.common.rotation as rot
    import fairchem.core.models.uma.outputs as outputs

    def _acos(x):
        return torch.acos(x.clamp(-1 + rot.EPS, 1 - rot.EPS))

    def _atan2(y, x):
        return torch.atan2(y, x)

    rot.Safeacos.apply = staticmethod(_acos)
    rot.Safeatan2.apply = staticmethod(_atan2)
    if getattr(outputs.compute_energy, "__wrapped__", None) is not None:
        outputs.compute_energy = outputs.compute_energy.__wrapped__
    elif hasattr(outputs.compute_energy, "_torchdynamo_inline"):
        pass
    # Drop the compiler.disable wrapper if present.
    fn = outputs.compute_energy
    inner = getattr(fn, "__wrapped__", None) or getattr(fn, "fn", None)
    if inner is not None:
        outputs.compute_energy = inner
    print("patched Safeacos/Safeatan2/compute_energy for export", flush=True)


def runtime_metadata(
    path: Path,
    cutoff: float,
    max_neighbors: int,
    example,
    dtype,
    *,
    task_name: str = "omol",
    charge: int = 0,
    spin: int = 1,
    z_set: list[int] | None = None,
    label: str = "hcn",
    molecular_box: float = 0.0,
):
    input_names = [
        "pos",
        "atomic_numbers",
        "cell",
        "pbc",
        "edge_index",
        "cell_offsets",
        "charge",
        "spin",
        "batch",
        "natoms",
    ]
    meta = {
        "cutoff": float(cutoff),
        "molecular_box": float(molecular_box),
        "max_neighbors": int(max_neighbors),
        "task_name": str(task_name),
        "charge": int(charge),
        "spin": int(spin),
        "z_set": [int(z) for z in (z_set or [])],
        "label": str(label),
        "edge_convention": "fairchem_neighbor_center",
        "inputs": input_names,
        "outputs": ["energy", "forces"],
        "pos_dtype": str(dtype).replace("torch.", ""),
        "shapes": {
            name: list(t.shape) for name, t in zip(input_names, example)
        },
    }
    return meta


def _first_import(candidates):
    last = None
    for spec in candidates:
        mod, name = spec.rsplit(".", 1)
        try:
            module = __import__(mod, fromlist=[name])
            return getattr(module, name)
        except Exception as exc:
            last = exc
    raise RuntimeError(f"none of {candidates} imported") from last


def aoti_package(exported, path: Path, metadata=None):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fn = _first_import(
        [
            "torch.export.aoti_compile_and_package",
            "torch._inductor.aoti_compile_and_package",
            "torch._inductor.package.aoti_compile_and_package",
        ]
    )
    kwargs = {"package_path": str(path)}
    if metadata:
        # Bake the runtime contract into the package itself: the C++
        # loader reads it via AOTIModelPackageLoader::get_metadata(),
        # so the .pt2 is self-describing and needs no sidecar.
        kwargs["inductor_configs"] = {
            "aot_inductor.metadata": {
                str(k): str(v) for k, v in metadata.items()
            }
        }
    fn(exported, **kwargs)
    return path


def load_aoti(path: Path):
    fn = _first_import(
        [
            "torch._inductor.aoti_load_package",
            "torch._inductor.package.load_package",
            "torch.export.load",
        ]
    )
    return fn(str(path))


def run_aoti(pkg, args):
    if hasattr(pkg, "run"):
        outs = pkg.run(list(args))
        if isinstance(outs, (list, tuple)):
            return outs[0], outs[1]
        return outs
    if callable(pkg):
        return pkg(*args)
    raise TypeError(type(pkg))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", default="uma-s-1p1")
    p.add_argument("--device", default="cpu")
    p.add_argument("--atoms", default=None, help="Structure (.xyz/.con). Default: Baker HCN.")
    p.add_argument("--charge", type=int, default=None, help="Total charge (default: atoms.info or 0)")
    p.add_argument("--spin", type=int, default=None, help="Spin multiplicity (default: atoms.info or 1)")
    p.add_argument("--label", default=None, help="Metadata / filename label (default: hcn or atoms path)")
    p.add_argument("--task", default="omol", help="FAIRChem task_name (default: omol)")
    p.add_argument("--out", default=None, help="Output .pt2 path")
    p.add_argument(
        "--molecular-box",
        type=float,
        default=0.0,
        help=(
            "Trace with the molecule re-centered in this synthetic cube and "
            "a cutoff of 0.4*L: every intramolecular pair is an edge, no "
            "periodic image ever is, so the vesin edge count is n(n-1) at "
            "every geometry (what a static-shape AOTI graph requires). "
            "The runtime reads molecular_box back from the package metadata."
        ),
    )
    p.add_argument("--skip-aoti", action="store_true")
    p.add_argument("--eager-only", action="store_true")
    p.add_argument(
        "--compare-only",
        action="store_true",
        help="Load an existing .pt2 and compare to ASE; do not export.",
    )
    args = p.parse_args()

    atoms = load_atoms(args.atoms)
    label = args.label or default_label(args.atoms)
    charge = args.charge if args.charge is not None else int(atoms.info.get("charge", 0))
    spin = args.spin if args.spin is not None else int(atoms.info.get("spin", 1))
    atoms.info["charge"] = int(charge)
    atoms.info["spin"] = int(spin)
    if args.molecular_box and args.molecular_box > 0.0:
        L = float(args.molecular_box)
        pos = atoms.get_positions()
        atoms.set_positions(pos - pos.mean(axis=0) + [L / 2.0] * 3)
        atoms.set_cell([[L, 0.0, 0.0], [0.0, L, 0.0], [0.0, 0.0, L]])
        atoms.set_pbc(True)
    task_name = str(args.task)
    z_set = z_set_of(atoms)
    if args.out is None:
        args.out = f"bench_data/uma/{args.model}-{task_name}-{label}.pt2"
    out = Path(args.out)

    print(
        f"label={label} task={task_name} charge={charge} spin={spin} "
        f"z_set={z_set} natoms={len(atoms)}",
        flush=True,
    )
    print("ASE FAIRChemCalculator reference", args.model, flush=True)
    e_ref, f_ref, pred = ase_reference(atoms, args.model, args.device, task_name)
    print(f"ASE FAIRChemCalculator E={e_ref:.17g}", flush=True)
    for i, row in enumerate(f_ref):
        print(f"  F[{i}] {row[0]:.17g} {row[1]:.17g} {row[2]:.17g}")

    hydra = pred.model
    backbone = hydra.module.backbone
    # ASE used internal graph gen. The exported module takes vesin edges.
    backbone.otf_graph = False
    cutoff = float(backbone.cutoff)
    max_n = int(getattr(backbone, "max_neighbors", 300) or 300)
    if args.molecular_box and args.molecular_box > 0.0:
        # Complete-graph tracing: the model envelope zeroes pairs beyond
        # its own cutoff, so widening the graph cutoff only fixes the
        # edge count (validated on claisen reactant + 1.35x stretch:
        # dE <= 3.0e-4 eV, dF <= 8.7e-4 eV/A vs the internal graph).
        cutoff = 0.4 * float(args.molecular_box)
        max_n = 100000
    print(f"cutoff={cutoff} max_neighbors={max_n}", flush=True)
    tasks = pred.dataset_to_tasks.get(task_name, [])
    for task in tasks:
        mean = float(task.normalizer.mean)
        rmsd = float(task.normalizer.rmsd)
        refs = task.element_references is not None
        print(
            f"task {task.name} prop={task.property} mean={mean:.6g} rmsd={rmsd:.6g} refs={refs}",
            flush=True,
        )

    device = torch.device(args.device)
    dtype = next(hydra.parameters()).dtype

    if args.compare_only:
        if not out.is_file():
            print(f"FAIL: --compare-only needs an existing .pt2: {out}", file=sys.stderr)
            return 4
        example = pack_example(atoms, cutoff, device, dtype, max_n)
        meta = runtime_metadata(
            out,
            cutoff,
            max_n,
            example,
            dtype,
            task_name=task_name,
            charge=charge,
            spin=spin,
            z_set=z_set,
            label=label,
            molecular_box=float(args.molecular_box or 0.0),
        )
        pkg = load_aoti(out)
        e_a, f_a = run_aoti(pkg, example)
        e_a = torch.as_tensor(e_a).detach().cpu()
        f_a = torch.as_tensor(f_a).detach().cpu().numpy()
        if not compare("aoti", e_a, f_a, e_ref, f_ref):
            print("FAIL: AOTI package does not match ASE", file=sys.stderr)
            return 4
        print("OK existing", out.resolve())
        return 0

    data_v = vesin_atomic_data(
        atoms, cutoff, dtype, device, max_n, task_name=task_name, sid=label
    )
    print(f"vesin AtomicData edges={int(data_v.nedges.item())}", flush=True)
    pred_v = pred.predict(data_v)
    e_ad = pred_v["energy"].detach().cpu()
    f_ad = pred_v["forces"].detach().cpu().numpy()
    ok_ad = compare("atomicdata+vesin", e_ad, f_ad, e_ref, f_ref)
    if not ok_ad:
        print("FAIL: vesin AtomicData via MLIPPredictUnit does not match ASE", file=sys.stderr)
        return 2

    example = pack_example(atoms, cutoff, device, dtype, max_n)
    print(f"vesin edges={example[4].shape[1]}", flush=True)

    wrap = TensorUma(hydra, pred_unit=pred, task_name=task_name).to(device).eval()
    with torch.enable_grad():
        e_e, f_e = wrap(*example)
    ok_eager = compare("eager+vesin", e_e.detach().cpu(), f_e.detach().cpu().numpy(), e_ref, f_ref)
    if not ok_eager:
        print("FAIL: eager tensor wrapper does not match ASE", file=sys.stderr)
        return 2
    meta = runtime_metadata(
        out,
        cutoff,
        max_n,
        example,
        dtype,
        task_name=task_name,
        charge=charge,
        spin=spin,
        z_set=z_set,
        label=label,
        molecular_box=float(args.molecular_box or 0.0),
    )
    if args.eager_only:
        return 0

    patch_export_ops()
    print("torch.export", flush=True)
    natoms_dim = torch.export.Dim("natoms", min=1, max=256)
    nedges_dim = torch.export.Dim("nedges", min=1, max=65536)
    dyn = {
        "pos": {0: natoms_dim},
        "atomic_numbers": {0: natoms_dim},
        "cell": None,
        "pbc": None,
        "edge_index": {1: nedges_dim},
        "cell_offsets": {0: nedges_dim},
        "charge": None,
        "spin": None,
        "batch": {0: natoms_dim},
        "natoms": None,
    }
    exported = None
    attempts = [
        ("dynamic-nonstrict", dict(dynamic_shapes=dyn, strict=False)),
        ("static-nonstrict", dict(strict=False)),
        ("static-strict", dict(strict=True)),
    ]
    last = None
    for attempt, kwargs in attempts:
        try:
            print("try export", attempt, flush=True)
            exported = torch.export.export(wrap, example, **kwargs)
            print("export ok", attempt, type(exported), flush=True)
            break
        except Exception as exc:
            last = exc
            print(f"{attempt} failed: {type(exc).__name__}: {exc}", flush=True)
    if exported is None:
        print("try make_fx(tracing_mode=real)", flush=True)
        from torch.fx.experimental.proxy_tensor import make_fx

        gm = make_fx(wrap, tracing_mode="real")(*example)
        print("make_fx nodes", len(gm.graph.nodes), flush=True)
        try:
            exported = torch.export.export(gm, example, strict=False)
            print("export ok make_fx-nonstrict", type(exported), flush=True)
        except Exception as exc:
            last = exc
            print(f"make_fx export failed: {type(exc).__name__}: {exc}", flush=True)
            raise last from exc

    print("export ok", type(exported), flush=True)
    with torch.enable_grad():
        e_x, f_x = exported.module()(*example)
    if not compare("exported", e_x.detach().cpu(), f_x.detach().cpu().numpy(), e_ref, f_ref):
        print("FAIL: exported module does not match ASE", file=sys.stderr)
        return 3

    if args.skip_aoti:
        return 0

    out = Path(args.out)
    print("AOTI package", out, flush=True)
    aoti_package(exported, out, metadata=meta)
    pkg = load_aoti(out)
    e_a, f_a = run_aoti(pkg, example)
    e_a = torch.as_tensor(e_a).detach().cpu()
    f_a = torch.as_tensor(f_a).detach().cpu().numpy()
    if not compare("aoti", e_a, f_a, e_ref, f_ref):
        print("FAIL: AOTI package does not match ASE", file=sys.stderr)
        return 4
    print("WROTE", out.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
