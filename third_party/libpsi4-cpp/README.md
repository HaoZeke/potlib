# libpsi4-cpp — pure C++ Psi4 core (no Python)

Builds a `libpsi4` shared library from the Psi4 C++ sources under
`ref_psi4/psi4/src/psi4/`, **excluding** Python/pybind export layers
(`export_*.cc`, `pybind*`, `python*`, driver bindings).

This library is loaded at runtime by rgpot's `Psi4Pot` frontend
(`dlopen(libpsi4, RTLD_GLOBAL)` then `dlopen(libpsi4_engine)`).

## Quick build

```bash
# From rgpot repo root:
./scripts/build_psi4_backend.sh
# or only the C++ core:
./third_party/libpsi4-cpp/build.sh
```

Install prefix defaults to `third_party/libpsi4-cpp/install/` with:

- `lib/libpsi4.so` (or `.dylib`)
- `include/psi4/...` headers copied/symlinked for engine compiles
- Psi4 share data is **not** duplicated; point `PSIDATADIR` at
  `ref_psi4/psi4/share/psi4` (or a conda/pixi psi4 datadir).

## Dependencies

Same as Psi4 core: C++20, BLAS/LAPACK, optional Libint2, Libxc, PCMSolver,
etc. The CMake wrapper is intentionally minimal; for a full-featured build,
configure Psi4 normally with `-D ENABLE_PYTHON=OFF` if available, or use this
wrapper to compile only non-export sources.

## Engine compile

```bash
meson setup build-psi4 -Dwith_psi4=true \
  -Dpsi4_includedir=$PWD/third_party/libpsi4-cpp/install/include
meson compile -C build-psi4
export LD_LIBRARY_PATH=$PWD/third_party/libpsi4-cpp/install/lib:$LD_LIBRARY_PATH
export RGPOT_PSI4_SO=$PWD/third_party/libpsi4-cpp/install/lib/libpsi4.so
export RGPOT_PSI4_ENGINE=$PWD/build-psi4/CppCore/rgpot/Psi4Pot/libpsi4_engine.so
export PSIDATADIR=$PWD/ref_psi4/psi4/share/psi4
```
