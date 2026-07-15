Python bindings use **nanobind** with **stable ABI** (abi3 / Py_LIMITED_API 3.12)
when built on Python >= 3.12 (same policy as pyeonclient). Metatomic engines are
packed multi-ABI under ``rgpot/lib/torch-X.Y/`` and selected from the installed
torch major at runtime.
