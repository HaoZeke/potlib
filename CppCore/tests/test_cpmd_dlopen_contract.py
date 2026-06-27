#!/usr/bin/env python3
"""Build contract checks for the CPMD pure-consumer boundary."""

from __future__ import annotations

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    cpmd_dir = root / "CppCore" / "rgpot" / "CPMDPot"
    meson = (cpmd_dir / "meson.build").read_text(encoding="utf-8")
    frontend = (cpmd_dir / "CPMDPot.cc").read_text(encoding="utf-8")

    forbidden = [
        "RGPOT_CPMD_STATIC_EMBED",
        "cpmd_static_embed_dep",
        "cpmd_embed_static",
        "cpmd_extra_link",
    ]
    for token in forbidden:
        require(token not in meson, f"meson.build leaks static embed token {token}")
        require(token not in frontend, f"CPMDPot.cc leaks static embed token {token}")
    for token in ["with_cpmd", "cpmd_root"]:
        require(token not in meson, f"meson.build leaks static embed token {token}")

    require(
        "cpmdpot_dep = declare_dependency(" in meson,
        "CPMD frontend dependency is missing",
    )
    require(
        "dependencies: [cpmd_dl_dep, ptlrpc_dep]" in meson,
        "CPMD frontend dependency should expose dl plus the Cap'n Proto schema dependency",
    )
    require(
        "libcpmdc" in frontend,
        "CPMDPot.cc must dlopen the split cpmdc engine (libcpmdc)",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
