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


def _add_input_blocks(params: Any, input_blocks: Iterable[str]) -> None:
    blocks = [str(block) for block in input_blocks]
    out = params.init("inputBlocks", len(blocks))
    for idx, block in enumerate(blocks):
        out[idx] = block


def _set_system_section(section: Any, *, system_cell: Sequence[float], cut_off_ry: float, charge: int, multiplicity: int) -> None:
    system = section.init("system")
    system.angstrom = True
    system.cutOffRy = float(cut_off_ry)
    system.charge = int(charge)
    system.multiplicity = int(multiplicity)
    cell = system.init("cell", len(system_cell))
    for idx, value in enumerate(system_cell):
        cell[idx] = float(value)


def _set_atoms_section(section: Any, pseudopotentials: Iterable[Any]) -> None:
    atoms = section.init("atoms")
    pseudo_specs = list(pseudopotentials)
    out = atoms.init("pseudopotentials", len(pseudo_specs))
    for idx, spec in enumerate(pseudo_specs):
        out[idx].element = str(_mapping_or_sequence_value(spec, "element", 0, ""))
        out[idx].path = str(_mapping_or_sequence_value(spec, "path", 1, ""))
        out[idx].lmax = int(_mapping_or_sequence_value(spec, "lmax", 2, -1))


def _set_set_section(section: Any, directive: Any) -> None:
    set_directive = section.init("set")
    set_directive.key = str(_mapping_or_sequence_value(directive, "key", 0, ""))
    set_directive.value = str(_mapping_or_sequence_value(directive, "value", 1, ""))


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
    section_count = int(system_cell is not None) + int(bool(pseudo_specs)) + len(set_specs)
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
