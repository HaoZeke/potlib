# libpsi4-cpp compile stubs

CMake points here via `STUB_INC` in `CMakeLists.txt`. Copy or symlink the
engine stubs before building if this tree is incomplete:

```bash
cp -a CppCore/rgpot/Psi4Pot/psi4_stubs/* third_party/libpsi4-cpp/stubs/
# ensure psi4/pybind11.h exists (CMakeLists also copies/links stubs/psi4/)
```

Those provide minimal `Python.h` / `pybind11` / `psi4/pybind11.h` so Psi4 C++
headers compile without a Python interpreter. **Do not link libpython.**

Runtime path: pure C++ `libpsi4.so` only (`RGPOT_PSI4_SO`). Conda
`core.cpython-*.so` is unsupported (needs `Py_*` symbols).
