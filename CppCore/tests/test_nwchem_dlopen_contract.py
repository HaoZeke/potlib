#!/usr/bin/env python3
"""Build contract checks for the NWChem pure-consumer boundary.

rgpot dlopens the split nwchemc engine (libnwchemc.so) at runtime and builds
no in-tree NWChem engine of its own. These checks pin that boundary: no static
embed, no local ABI/param mirror, and no shared_library engine target.
"""

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

    mirror_forbidden = [
        "RgpotNWChemParams",
        "nwchemParamsToAbi",
        "nwchemAbiToParams",
        "nwchemAbiDefaults",
        "nwchemAbiSummary",
        "abiParams",
    ]
    mirror_files = [
        nwchem_dir / "NWChemPot.cc",
        nwchem_dir / "NWChemPot.hpp",
        root / "CppCore" / "rgpot" / "types" / "adapters" / "capnp" / "nwchem_capnp_map.hpp",
        root / "CppCore" / "tests" / "NWChemCapnpMapTest.cc",
        root / "CppCore" / "tests" / "NWChemPotTest.cc",
        root / "CppCore" / "tests" / "test_nwchem_abi_main.c",
    ]
    for path in mirror_files:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for token in mirror_forbidden:
            require(token not in text, f"{path.relative_to(root)} keeps local ABI mirror token {token}")

    # Pure consumer: rgpot builds NO in-tree NWChem engine. The split nwchemc
    # engine (libnwchemc.so) is resolved by dlopen at runtime only.
    pure_consumer_forbidden = [
        "shared_library(",
        "'nwchem_engine'",
        "with_nwchem",
        "nwchem_root",
        "nwchem_embed",
        "RGPOT_HAS_NWCHEM=",
    ]
    for token in pure_consumer_forbidden:
        require(
            token not in meson,
            f"meson.build builds an in-tree engine ({token}); rgpot must be a pure nwchemc consumer",
        )

    require(
        "nwchempot_dep = declare_dependency(" in meson,
        "NWChem frontend dependency is missing",
    )
    require(
        "dependencies: [nwchem_dl_dep, ptlrpc_dep]" in meson,
        "NWChem frontend dependency should expose dl plus the Cap'n Proto schema dependency",
    )
    # The frontend resolves the engine by dlopen, keyed on libnwchemc.
    require(
        "libnwchemc" in frontend,
        "NWChemPot.cc must dlopen the split nwchemc engine (libnwchemc)",
    )
    require(
        "libnwchem_engine" not in frontend,
        "NWChemPot.cc still references the dropped in-tree libnwchem_engine",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
