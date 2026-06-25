#!/usr/bin/env python3
"""Build contract checks for the NWChem runtime-loaded engine boundary."""

from __future__ import annotations

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    nwchem_dir = root / "CppCore" / "rgpot" / "NWChemPot"
    meson = (nwchem_dir / "meson.build").read_text(encoding="utf-8")
    frontend = (nwchem_dir / "NWChemPot.cc").read_text(encoding="utf-8")

    forbidden = [
        "RGPOT_NWCHEM_STATIC_EMBED",
        "nwchem_static_embed_dep",
        "nwchem_embed_static",
        "nwchem_extra_link",
    ]
    for token in forbidden:
        require(token not in meson, f"meson.build leaks static embed token {token}")
        require(token not in frontend, f"NWChemPot.cc leaks static embed token {token}")

    require("shared_library(" in meson, "NWChem engine is not a shared_library")
    require("'nwchem_engine'" in meson, "NWChem engine target is missing")
    require(
        "build_by_default: false" not in meson,
        "with_nwchem must build the runtime-loaded engine by default",
    )
    require(
        "nwchempot_dep = declare_dependency(" in meson,
        "NWChem frontend dependency is missing",
    )
    require(
        "dependencies: [nwchem_dl_dep]" in meson,
        "NWChem frontend dependency should expose only dl",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
