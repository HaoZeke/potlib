"""rgpot — potential energy surfaces for atomistic simulation.

Core install provides always-on in-process pots (Lennard-Jones). Optional
backends (RPC potserv, Metatomic, NWChem, CPMD) remain separate build extras.
"""

from __future__ import annotations

from rgpot._core import LJPot, __version__, evaluate_lj

__all__ = [
    "LJPot",
    "evaluate_lj",
    "__version__",
]
