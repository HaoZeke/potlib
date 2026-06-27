#!/usr/bin/env python3
"""Helpers for rgpot PotentialConfig with the cpmd arm."""

from __future__ import annotations

import os
from collections.abc import Iterable, Mapping, Sequence
from typing import Any


def _engine_path_from_env() -> str:
    return (
        os.environ.get("CPMDC_LIBRARY", "")
        or os.environ.get("RGPOT_CPMDC_ENGINE", "")
        or os.environ.get("RGPOT_CPMD_ENGINE", "")
    )


def _as_text_list(values: Iterable[Any]) -> list[str]:
    return [str(value) for value in values]


def _mapping_or_sequence_value(item: Any, key: str, index: int, default: Any = None) -> Any:
    if isinstance(item, Mapping):
        return item.get(key, default)
    if isinstance(item, Sequence) and not isinstance(item, (str, bytes)):
        return item[index] if index < len(item) else default
    return default


def _mapping_value(item: Any, keys: Sequence[str], default: Any = None) -> Any:
    if isinstance(item, Mapping):
        for key in keys:
            if key in item:
                return item[key]
    return default


def _add_input_blocks(params: Any, input_blocks: Iterable[str]) -> None:
    blocks = [str(block) for block in input_blocks]
    out = params.init("inputBlocks", len(blocks))
    for idx, block in enumerate(blocks):
        out[idx] = block


def _set_system_section(
    section: Any,
    *,
    system_cell: Sequence[float],
    cut_off_ry: float,
    charge: int,
    multiplicity: int,
    symmetry: int = 0,
    angstrom: bool = True,
    scale: float = 0.0,
    directives: Iterable[Any] = (),
) -> None:
    system = section.init("system")
    system.symmetry = int(symmetry)
    system.angstrom = bool(angstrom)
    system.cutOffRy = float(cut_off_ry)
    system.scale = float(scale)
    system.charge = int(charge)
    system.multiplicity = int(multiplicity)
    cell = system.init("cell", len(system_cell))
    for idx, value in enumerate(system_cell):
        cell[idx] = float(value)
    _set_directives(system, directives)


def _set_atoms_section(
    section: Any,
    pseudopotentials: Iterable[Any],
    directives: Iterable[Any] = (),
) -> None:
    atoms = section.init("atoms")
    pseudo_specs = list(pseudopotentials)
    out = atoms.init("pseudopotentials", len(pseudo_specs))
    for idx, spec in enumerate(pseudo_specs):
        out[idx].element = str(_mapping_or_sequence_value(spec, "element", 0, ""))
        out[idx].path = str(_mapping_or_sequence_value(spec, "path", 1, ""))
        out[idx].lmax = int(_mapping_or_sequence_value(spec, "lmax", 2, -1))
    _set_directives(atoms, directives)


def _set_directives(owner: Any, directives: Iterable[Any]) -> None:
    specs = list(directives)
    if not specs:
        return
    out = owner.init("directives", len(specs))
    for idx, spec in enumerate(specs):
        out[idx].keyword = str(_mapping_or_sequence_value(spec, "keyword", 0, ""))
        args = _mapping_or_sequence_value(spec, "args", 1, ())
        if isinstance(args, (str, bytes)):
            arg_values = [args]
        else:
            arg_values = list(args)
        arg_out = out[idx].init("args", len(arg_values))
        for arg_idx, value in enumerate(arg_values):
            arg_out[arg_idx] = str(value)


def _set_set_section(section: Any, directive: Any) -> None:
    set_directive = section.init("set")
    set_directive.key = str(_mapping_or_sequence_value(directive, "key", 0, ""))
    set_directive.value = str(_mapping_or_sequence_value(directive, "value", 1, ""))


def _set_generic_section(section: Any, spec: Any) -> None:
    generic = section.init("generic")
    generic.name = str(_mapping_or_sequence_value(spec, "name", 0, ""))
    _set_directives(generic, _mapping_value(spec, ["directives"], ()))


def _set_cpmd_section(section: Any, spec: Any) -> None:
    cpmd = section.init("cpmd")
    cpmd.optimizeWavefunction = bool(
        _mapping_value(spec, ["optimizeWavefunction", "optimize_wavefunction"], True)
    )
    cpmd.optimizeGeometry = bool(
        _mapping_value(spec, ["optimizeGeometry", "optimize_geometry"], False)
    )
    cpmd.molecularDynamics = bool(
        _mapping_value(spec, ["molecularDynamics", "molecular_dynamics"], False)
    )
    cpmd.molecularDynamicsCp = bool(
        _mapping_value(spec, ["molecularDynamicsCp", "molecular_dynamics_cp"], False)
    )
    cpmd.molecularDynamicsBo = bool(
        _mapping_value(spec, ["molecularDynamicsBo", "molecular_dynamics_bo"], False)
    )
    cpmd.molecularDynamicsEh = bool(
        _mapping_value(spec, ["molecularDynamicsEh", "molecular_dynamics_eh"], False)
    )
    cpmd.molecularDynamicsPt = bool(
        _mapping_value(spec, ["molecularDynamicsPt", "molecular_dynamics_pt"], False)
    )
    cpmd.molecularDynamicsClassical = bool(
        _mapping_value(
            spec,
            ["molecularDynamicsClassical", "molecular_dynamics_classical"],
            False,
        )
    )
    cpmd.molecularDynamicsFile = str(
        _mapping_value(spec, ["molecularDynamicsFile", "molecular_dynamics_file"], "")
    )
    cpmd.convergenceOrbitals = float(
        _mapping_value(spec, ["convergenceOrbitals", "convergence_orbitals"], 1.0e-6)
    )
    cpmd.convergenceGeometry = float(
        _mapping_value(spec, ["convergenceGeometry", "convergence_geometry"], 0.0)
    )
    cpmd.maxStep = int(_mapping_value(spec, ["maxStep", "max_step"], 0))
    cpmd.maxIter = int(_mapping_value(spec, ["maxIter", "max_iter"], 0))
    cpmd.timestep = float(_mapping_value(spec, ["timestep"], 0.0))
    cpmd.electronMass = float(
        _mapping_value(spec, ["electronMass", "electron_mass"], 0.0)
    )
    cpmd.nose = bool(_mapping_value(spec, ["nose"], False))
    cpmd.noseIons = bool(_mapping_value(spec, ["noseIons", "nose_ions"], False))
    cpmd.noseElectrons = bool(
        _mapping_value(spec, ["noseElectrons", "nose_electrons"], False)
    )
    cpmd.berendsen = str(_mapping_value(spec, ["berendsen"], ""))
    cpmd.langevin = bool(_mapping_value(spec, ["langevin"], False))
    cpmd.annealing = str(_mapping_value(spec, ["annealing"], ""))
    cpmd.quench = bool(_mapping_value(spec, ["quench"], False))
    cpmd.rattle = bool(_mapping_value(spec, ["rattle"], False))
    cpmd.shake = bool(_mapping_value(spec, ["shake"], False))
    cpmd.constraint = str(_mapping_value(spec, ["constraint"], ""))
    cpmd.trotter = str(_mapping_value(spec, ["trotter"], ""))
    cpmd.restart = bool(_mapping_value(spec, ["restart"], False))
    cpmd.restartWavefunction = bool(
        _mapping_value(spec, ["restartWavefunction", "restart_wavefunction"], False)
    )
    cpmd.trajectory = bool(_mapping_value(spec, ["trajectory"], False))
    _set_directives(cpmd, _mapping_value(spec, ["directives"], ()))


def _set_dft_section(section: Any, spec: Any) -> None:
    dft = section.init("dft")
    dft.functional = str(_mapping_value(spec, ["functional"], "BLYP"))
    dft.lsd = bool(_mapping_value(spec, ["lsd"], False))
    dft.gcCutoff = float(_mapping_value(spec, ["gcCutoff", "gc_cutoff"], 0.0))
    dft.xcDriver = str(_mapping_value(spec, ["xcDriver", "xc_driver"], ""))
    dft.libxc = str(_mapping_value(spec, ["libxc"], ""))
    dft.lrKernel = str(_mapping_value(spec, ["lrKernel", "lr_kernel"], ""))
    dft.refunct = str(_mapping_value(spec, ["refunct"], ""))
    dft.mtsHighFunc = str(
        _mapping_value(spec, ["mtsHighFunc", "mts_high_func"], "")
    )
    dft.mtsLowFunc = str(_mapping_value(spec, ["mtsLowFunc", "mts_low_func"], ""))
    dft.hfx = bool(_mapping_value(spec, ["hfx"], False))
    dft.hfxScreening = str(
        _mapping_value(spec, ["hfxScreening", "hfx_screening"], "")
    )
    dft.hubbard = str(_mapping_value(spec, ["hubbard"], ""))
    dft.alpha = float(_mapping_value(spec, ["alpha"], 0.0))
    dft.beta = float(_mapping_value(spec, ["beta"], 0.0))
    dft.oldCode = bool(_mapping_value(spec, ["oldCode", "old_code"], False))
    dft.newCode = bool(_mapping_value(spec, ["newCode", "new_code"], False))
    dft.correlation = str(_mapping_value(spec, ["correlation"], ""))
    dft.exchange = str(_mapping_value(spec, ["exchange"], ""))
    dft.becke88 = bool(_mapping_value(spec, ["becke88"], False))
    _set_directives(dft, _mapping_value(spec, ["directives"], ()))


def _set_raw_section(section: Any, spec: Any) -> None:
    section.raw = str(_mapping_value(spec, ["text", "raw"], ""))


def _set_input_section(section: Any, spec: Any) -> None:
    kind = str(_mapping_or_sequence_value(spec, "kind", 0, "")).lower()
    if kind == "generic":
        _set_generic_section(section, spec)
    elif kind == "system":
        _set_system_section(
            section,
            system_cell=_mapping_value(spec, ["cell", "system_cell"], ()),
            cut_off_ry=float(_mapping_value(spec, ["cutOffRy", "cut_off_ry"], 70.0)),
            charge=int(_mapping_value(spec, ["charge"], 0)),
            multiplicity=int(_mapping_value(spec, ["multiplicity"], 1)),
            symmetry=int(_mapping_value(spec, ["symmetry"], 0)),
            angstrom=bool(_mapping_value(spec, ["angstrom"], True)),
            scale=float(_mapping_value(spec, ["scale"], 0.0)),
            directives=_mapping_value(spec, ["directives"], ()),
        )
    elif kind == "cpmd":
        _set_cpmd_section(section, spec)
    elif kind == "dft":
        _set_dft_section(section, spec)
    elif kind == "atoms":
        _set_atoms_section(
            section,
            _mapping_value(spec, ["pseudopotentials"], ()),
            _mapping_value(spec, ["directives"], ()),
        )
    elif kind == "set":
        _set_set_section(section, spec)
    elif kind == "raw":
        _set_raw_section(section, spec)
    else:
        raise ValueError(f"unknown CPMD input section kind: {kind}")


def make_cpmd_params(
    pot_capnp: Any,
    *,
    functional: str = "BLYP",
    cut_off_ry: float = 70.0,
    charge: int = 0,
    multiplicity: int = 1,
    task: str = "gradient",
    title: str = "",
    memory_mb: int = 0,
    scratch_dir: str = "",
    permanent_dir: str = "",
    cpmd_root: str = "",
    engine_path: str = "",
    input_blocks: Iterable[str] = (),
    system_cell: Sequence[float] | None = None,
    pseudopotentials: Iterable[Any] = (),
    set_directives: Iterable[Any] = (),
    input_sections: Iterable[Any] = (),
):
    """Build CPMDParams for the cpmd PotentialConfig arm."""
    params = pot_capnp.CPMDParams.new_message()
    params.functional = functional
    params.cutOffRy = float(cut_off_ry)
    params.charge = int(charge)
    params.multiplicity = int(multiplicity)
    params.task = task
    params.title = title
    params.memoryMb = int(memory_mb)
    params.scratchDir = scratch_dir
    params.permanentDir = permanent_dir
    params.cpmdRoot = cpmd_root or os.environ.get("CPMD_ROOT", "")
    params.enginePath = engine_path or _engine_path_from_env()

    block_list = _as_text_list(input_blocks)
    if block_list:
        _add_input_blocks(params, block_list)

    pseudo_specs = list(pseudopotentials)
    set_specs = list(set_directives)
    input_section_specs = list(input_sections)
    section_count = (
        int(system_cell is not None)
        + int(bool(pseudo_specs))
        + len(set_specs)
        + len(input_section_specs)
    )
    if section_count:
        sections = params.init("inputSections", section_count)
        idx = 0
        if system_cell is not None:
            _set_system_section(
                sections[idx],
                system_cell=system_cell,
                cut_off_ry=cut_off_ry,
                charge=charge,
                multiplicity=multiplicity,
            )
            idx += 1
        if pseudo_specs:
            _set_atoms_section(sections[idx], pseudo_specs)
            idx += 1
        for directive in set_specs:
            _set_set_section(sections[idx], directive)
            idx += 1
        for section_spec in input_section_specs:
            _set_input_section(sections[idx], section_spec)
            idx += 1

    return params


def make_potential_config_cpmd(pot_capnp: Any, **kwargs):
    """Build PotentialConfig with the cpmd union arm set."""
    cfg = pot_capnp.PotentialConfig.new_message()
    cfg.cpmd = make_cpmd_params(pot_capnp, **kwargs)
    return cfg


async def configure_cpmd(pot, pot_capnp: Any, **kwargs) -> tuple[bool, str]:
    """Call Potential.configure with CPMDParams; return (ok, message)."""
    cfg = make_potential_config_cpmd(pot_capnp, **kwargs)
    result = await pot.configure(cfg)
    return bool(result.ok), str(result.message)
