Python bindings use **nanobind** with **stable ABI** (abi3 / Py_LIMITED_API 3.12)
when built on Python >= 3.12 (same policy as pyeonclient). Metatomic engines are
packed multi-ABI under ``rgpot/lib/torch-X.Y/`` and selected from the installed
torch major at runtime. Supported libtorch majors start at **2.7** (engines for
2.7–2.13 ship in the manylinux wheel); torch 2.6 and older are not bundled.
