# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- towncrier release notes start -->

## [3.0.0](https://github.com/OmniPotentRPC/rgpot/tree/3.0.0) - 2026-07-26

### Added

- Pot-hosting groundwork for absorbing eOn's potential kernels:
  `PotCaps` capability descriptors (`PotentialBase::caps()`) replace
  caller-side thread-safety lists; `paramsKey()` parameter fingerprints
  (FNV-1a + kernel-version salt) join the result-cache key so instances
  with different parameters never share entries; `LJConfig` establishes
  the plain-aggregate parameter convention; a `pot_bench` Catch2
  microbenchmark harness gates future pot migrations; `potctl` locksteps
  `pyproject.toml`. ([#57](https://github.com/OmniPotentRPC/rgpot/issues/57))
- Eight potentials absorbed from eOn arrive as Fortran 2018 kernels under
  `CppCore/rgpot/fortran/`: Stillinger-Weber, EDIP, Lenosky, Tersoff, EAM
  aluminium, FeHe, CuH2, and TIP4P-H. Each is a rewrite rather than a
  wrapper -- modules with `implicit none`, derived-type parameters in place
  of COMMON blocks, kinds from `iso_fortran_env` asserted against the C
  types at compile time, `intent` on every argument, `pure` kernels,
  structured control flow, and status returns instead of `stop`.

  Neighbours come from vesin through `rgpot_neighbors`, a CSR full-list
  table carrying pair vectors and distances, and the vendored vesin Fortran
  interface gained the C API's Verlet `skin` option so the kernels keep the
  list caching their predecessors had. Pair sums are restated as gathers:
  each atom accumulates the whole force acting on it and writes one column,
  so the atom loops run under `do concurrent`. Bond-order and embedding
  kernels reach the same form in three passes, since the environment
  derivative is not known until its sum completes.

  Each kernel carries a Fortran-only test (translation invariance, zero net
  force, analytic forces against central differences of the energy) and is
  pinned in `FortranPotsTest` to the reference energies of the kernels it
  replaces. The kernel archives link under `--exclude-libs`, so no Fortran
  symbol reaches the library interface; a test fails the suite if one ever
  does.
- Three classical potentials absorbed from eOn, always built: `MorsePot`
  (pairwise Morse with the shifted cutoff, platinum defaults through
  `MorseConfig`), `LJClusterPot` (12-6 Lennard-Jones on free boundaries,
  `LJClusterConfig`), and `ZBLPot` (screened nuclear repulsion with the
  LAMMPS switching function, `ZBLConfig{cut_inner, cut_global}`, which
  rejects a cutoff pair outside `0 < cut_inner < cut_global`). All three
  take their pairs from the shared `PairListCache`, carry `paramsKey()`
  fingerprints, and appear in `pot_bench`. `PotType` gains `Morse`,
  `LJCluster` and `ZBL`.

### Changed

- `LJPot` pair search goes through `rgpot::nlist::PairListCache` (header-only
  port of eOn's PairListCache): Verlet-skin cached candidate lists in a
  process-global proximity-matched pool, lazy list capture so one-shot
  evaluations run a single fused scan, exact per-call MIC folding, and the
  inverse-r2 pair math instead of `pow()`/`sqrt()`. Boxes where MIC caching
  is unsound keep the historical per-call scan. ([#56](https://github.com/OmniPotentRPC/rgpot/issues/56))
- One versioned `librgpot.so.3` umbrella: every pot builds as a PIC static
  convenience library folded in via `link_whole`; only dlopen'd engine
  plugins ship as separate shared objects. `rgpot.pc` always installs when
  rgpot is the top-level project, exporting the umbrella with
  `RGPOT_HAS_*` feature cflags. vesin resolves once at top level
  (installed >=0.6 preferred, vendored `third_party/vesin` fallback with
  the Fortran interface), and the new `with_fortran_pots` feature
  decouples Fortran-backed potentials from the RPC role options.
  Statistics counters in `registry<T>` are atomic, and public headers no
  longer inject `AtomMatrix` at namespace scope. CI gains the first
  Windows leg and an offline `--wrap-mode=nodownload` rehearsal of the
  release-tarball build. ([#57](https://github.com/OmniPotentRPC/rgpot/issues/57))
- The Cap'n Proto schema ships inside `librgpot` instead of a separate
  `libptlrpc.so`. Consumers that bundle the umbrella alone -- pip wheels
  especially -- no longer need a second shared object beside it, and
  `rgpot.pc` correspondingly stops listing `-lptlrpc`; the symbols are
  unchanged and still exported from `librgpot`. `potserv` and
  `pot_client_bridge` take the generated translation unit from the shared
  archive rather than compiling their own copy.
- The CuH2 potential is the in-tree Fortran 2018 kernel
  (`CppCore/rgpot/fortran/rgpot_cuh2.f90`), reached through
  `rgpot::fortranpots::CuH2Pot` from `rgpot/fortran/FortranPots.hpp`. It
  matches the energy and forces of the kernel it replaces to 1e-6 on the
  geometry `CuH2PotTest` pins. `rgpot::CuH2Pot` and
  `rgpot/CuH2/CuH2Pot.hpp` are gone, and with them the `fortcuh2`
  subproject and its wrap-git entry, so a source build needs no download
  for the Fortran pots and the offline `--wrap-mode=nodownload` CI leg
  covers them. `rgpot/CuH2/cuh2Utils.hpp` keeps the slab-geometry helpers
  unchanged. In-tree sources gate the Fortran pots on
  `RGPOT_HAS_FORTRAN_POTS`, the same cflag `rgpot.pc` exports to
  consumers.

### Fixed

- `potctl`'s lockstep check now covers `pyproject.toml`, so the Python
  package version cannot drift away from meson, CMake, cargo, pixi, and
  towncrier again. It had: 2.5.4 went to PyPI as a multi-ABI wheel set
  (torch 2.7 through 2.13) while every other surface stayed at 2.5.3. All
  six now read the same version, and the check fails the build when they
  do not. ([#52](https://github.com/OmniPotentRPC/rgpot/issues/52))
- Four link-layout defects in the umbrella build, each of which only
  surfaced in a profile that combines the RPC stack with the Fortran
  potentials:

  - Every convenience archive carried its own copy of the vendored vesin
    translation unit, because meson copies a static library's objects into
    each static library that links it, so the umbrella saw multiple
    definitions of vesin's thread-local error state. The archives take a
    headers-only view now and the objects enter `librgpot` -- and each leaf
    binary -- exactly once.
  - `librgpot` linked without the Fortran runtime: it is a C++ target that
    takes the kernels through `link_whole`, which leaves meson no Fortran
    source to infer the runtime from. It is now named per compiler id.
  - `ptlrpc_dep` exported the generated capnp `.cpp` as a dependency source,
    compiling a second copy of that translation unit into every consumer.
    Only the generated header propagates.
  - The umbrella exported the RPC entry points only when something inside
    `librgpot` happened to reference them; they are folded in with
    `link_whole` now, since it is consumers that call them.
- The RPC integration test compares the CuH2 energy and forces at the same
  1e-6 tolerance `CuH2PotTest` uses, instead of asserting exact float
  equality against values from the kernel the Fortran 2018 rewrite replaced.
  Its error path also kills the server before draining its output: reading a
  live server's stderr blocks until that process exits, so an assertion
  failure did not report itself, it sat until the six-hour CI timeout. The
  handler now prints the exception type and traceback as well, since
  `AssertionError` stringifies to nothing.
- The strict-determinism metatomic test no longer requires two matched force
  calls to agree bit for bit, because the provider does not offer that.
  Instrumenting the path shows rgpot handing the model identical inputs --
  positions, cell, and neighbour list all hash the same on every SO3 pass of
  both calls -- and getting energies back that differ by one to two ulp on a
  call chosen at random. It reproduces with `OMP_NUM_THREADS=1`, with the
  TorchScript profiling executor frozen, and with the pair vectors copied
  into torch-owned storage, so it is neither thread-count reassociation, nor
  graph specialization, nor alignment of the buffers rgpot passes in. The
  test now bounds the energy at four ulp and the force components at an
  absolute floor scaled to the largest component, which still fails on any
  drift larger than the provider's own noise.
- The vendored vesin Fortran interface builds under LLVM Flang on Windows.
  It bound `vesin_neighbors` directly, which takes the device and options
  structs by value; Flang cannot lower a by-value `BIND(C)` derived type on
  the `x86_64-pc-windows-msvc` target and aborts the compiler outright
  ("not yet implemented: passing VALUE BIND(C) derived type for this
  target"), so no Fortran consumer of vesin could be built there at all.
  vesin gains `vesin_neighbors_byref`, which takes both structs through
  pointers and forwards them by value, and the Fortran module binds to that.
  Marked as an upstream candidate alongside the other local vesin patches.


## [2.5.3](https://github.com/OmniPotentRPC/rgpot/tree/2.5.3) - 2026-07-20

### Fixed

- Fix ``-Dwith_rpc_client_only=true -Dwith_tests=true``: use ``pot_bridge_dep``
  (was undefined ``rgpot_bridge_dep``), link ``units.cc`` into unit tests when
  ``rgpot_core`` is not built, and soft-skip bridge stress cases when potserv is
  not running (lazy client connect). Catch2 configure/link/test succeed for the
  client frontend product without a live server.


## [2.5.2](https://github.com/OmniPotentRPC/rgpot/tree/2.5.2) - 2026-07-18

### Fixed

- Rebuild Metatomic engines against vesin 0.6+ ``VesinOptions`` (``skin`` /
  ``n_threads``) so ``vesin_neighbors`` no longer fails with a bare error when
  runtime libvesin is 0.6 and the engine was built for 0.5.


## [2.5.0](https://github.com/OmniPotentRPC/rgpot/tree/2.5.0) - 2026-07-17

### Added

- Multi-language Codecov coverage (Rust, C++, Python, Fortran) with OIDC uploads. ([#47](https://github.com/OmniPotentRPC/rgpot/issues/47))
- Install headers and ``rgpot.pc`` (``nwchempot`` / ``cpmdpot`` / ``ptlrpc``; no
  torch or xTB at link time) so eOn and other hosts can prefer
  ``dependency('rgpot')`` over the Meson subproject wrap. Engines stay runtime
  dlopen. See ``docs/eon_pkgconfig.md``.
- Keep Metatomic **dlopen** product on pip: portable `libmetatomic_engine.so` plugin (no eonclib) + `evaluate_metatomic` frontend (not LJ-only wheels).
- Metatomic dual path: linked ``MetatomicPot`` (fast) and ``MetatomicDlopen``
  frontend plus optional ``libmetatomic_engine.so`` C ABI (slow/plugin path).
- Metatomic engines are built for each supported torch major
  (``rgpot/lib/torch-X.Y/libmetatomic_engine.so``) and selected at runtime from
  the installed torch version — same multi-ABI model as metatomic-torch itself.
  The pip product covers **torch 2.7 and newer**; earlier majors are out of scope.
- MetatomicConfig gains an explicit ``torch_determinism`` policy
  (``TorchDeterminismPolicy::Fast`` default, ``Strict`` opt-in). Strict mode
  enables deterministic LibTorch algorithms, math-only scaled-dot-product
  attention (flash / memory-efficient / cuDNN SDP disabled), deterministic
  cuDNN with benchmarking off, deterministic fill of uninitialized memory, and
  disables TF32 for cuBLAS and cuDNN. These flags are process-global via
  ``at::globalContext()``; Fast never mutates them. CUDA hosts still need
  ``CUBLAS_WORKSPACE_CONFIG=:4096:8`` (or ``:16:8``) before the first cuBLAS
  call for bit-stable matmuls.
- Portable Metatomic engine wheels: `$ORIGIN` RUNPATH to site-packages torch/metatomic
  (single build-time torch major; multi-ABI package dirs cannot be listed oldest-first).
  `scripts/rgpot_build_wheel.sh` always runs RPATH repair after `python -m build`.
- Python bindings use **nanobind** with **stable ABI** (abi3 / Py_LIMITED_API 3.12)
  when built on Python >= 3.12 (same policy as pyeonclient). Metatomic engines are
  packed multi-ABI under ``rgpot/lib/torch-X.Y/`` and selected from the installed
  torch major at runtime. Supported libtorch majors start at **2.7** (engines for
  2.7–2.13 ship in the manylinux wheel); torch 2.6 and older are not bundled.
- Python package `rgpot` is pip-installable: core Lennard-Jones bindings via
  meson-python wheels (`import rgpot; rgpot.evaluate_lj(...)`), plus optional
  Metatomic multi-ABI engines for **torch 2.7+**.
- xTB dual backends: keep linked ``XTBPot`` and add ``XTBDlopen`` +
  ``libxtb_engine.so`` C ABI plugin (same pattern as metatomic engine).

### Fixed

- Metatomic C ABI engines soft-release the GIL so pyeonclient Job.run can evaluate forces under torch autograd without a fat metatomic link.
- SoftGilRelease in the metatomic C ABI only calls PyEval_SaveThread when PyGILState_Check is true, so nested GIL release from pyeonclient Job.run is safe.


## [2.2.1](https://github.com/OmniPotentRPC/rgpot/tree/2.2.1) - 2026-07-06

### Fixed

- Windows/MSVC builds no longer fail on unused ``cxxabi.h`` includes or nested
  ``std::array`` box flattening in ``Potential`` (``C1083`` / ``C2676``).


## [2.2.0](https://github.com/OmniPotentRPC/rgpot/tree/2.2.0) - 2026-07-05

### Added

- A profile-driven ABI loader (`rgpot::abi::ProfileLoader`) resolves the minimum
  potential ABI available from a single install prefix.

### Changed

- The canonical potentials-schema contract is pinned at v1.13.0, adding the
  Capabilities discovery surface; the in-tree `Potentials.capnp` copies track it
  byte-for-byte.

### Fixed

- `rgpot-core` publishes to crates.io again: `dlpk` (0.1.5) and `eindir-core`
  (0.5.0, `capi`) now resolve from the registry instead of git pins, and the
  `publish = false` guard is gone.


## [2.1.0](https://github.com/OmniPotentRPC/rgpot/tree/2.1.0) - 2026-07-03

### Added

- eOn integration: eOn ships an in-process `RgpotPot` potential (`-Dwith_rgpot=true`) that consumes rgpot's `NWChemPot` / `CPMDPot` frontends as a Meson subproject and `dlopen`s `libnwchemc` / `libcpmdc` directly, with `potserv` remaining available for out-of-process RPC. See `docs/orgmode/howto/eon-rgpot.org`.

### Changed

- The Cap'n Proto schema is pinned to canonical [potentials-schema](https://github.com/OmniPotentRPC/potentials-schema) v1.12.0: `MetatomicParams` arm (union ordinal 4) with the upstream requested-outputs surface, typed NWChem and CPMD parity batches (dplot/esp, prop/linres/pimd/path/tddft), `CommonMethodSpec` overlay, and a schema-sync CI gate that fails when the vendored copies diverge from the pinned release.

### Fixed

- Release-prepare CI no longer runs `cargo publish --dry-run` for `rgpot-core` while it depends on git-only `dlpk` / `eindir-core` (not on crates.io). The job uses `cargo check -p rgpot-core --locked`, and `package.publish` is `false` until registry deps exist. ([#42](https://github.com/OmniPotentRPC/rgpot/issues/42))


## [2.0.0](https://github.com/OmniPotentRPC/rgpot/tree/2.0.0) - 2026-06-26

> Channel note: `v2.0.0` was prepared on `main` but never tagged or published;
> the content below first ships in `v2.1.0`.

### Added

- NWChemPot backend: stable message-based C ABI (`nwchem_c_abi.h`), always-built frontend with `dlopen` of optional `libnwchemc`, stub ABI for CI without NWChem, Cap'n Proto `NWChemParams`/`configure @1`, and `potserv ... NWChem`.
- CPMDPot backend: always-built frontend with `dlopen` of optional `libcpmdc` (split [`cpmdc`](https://github.com/OmniPotentRPC/cpmdc) engine), Cap'n Proto `CPMDParams` / `PotentialConfig.cpmd` / `configure`, structured `CPMDInputSection` arms, in-tree `cpmdc_fake_engine` for CI without CPMD, and `potserv ... CPMD`. Engine lookup: `CPMDC_LIBRARY`, `RGPOT_CPMDC_ENGINE`, `RGPOT_CPMD_ENGINE`, then `enginePath` on params.
- rgpot potentials are now eindir objectives: `rgpot_potential_t` embeds eindir's `eindir_objective_t` as its first member (zero-cost IS-A), with the embedded eval/grad callbacks routed through the rgpot force callback (gradient = -force). rgpot-core consumes `eindir-core` as a shared Cargo crate rather than a prebuilt static lib, so downstream Rust consumers (e.g. `anneal-core`) can minimize an rgpot potential through `eindir_core::Objective<f64>` without a two-Rust-runtime conflict. See `docs/orgmode/howto/eindir-anneal.org`.

### Developer

- CI authoring uses a Nickel `ci/gha/` library with a single hand-maintained
  `ci-orchestrator.yml` for PR/main jobs (prepare/plan, hygiene, build/rust/bridge,
  potentials, CI gate), and nickel-exported workflows only for release, docs,
  cosmo potctl, and doc-commenter. Removed redundant `build`/`prek`/`potentials`/
  `docs_quality`/`towncrier`/`cosmo-potctl-spike` committed workflows in favor of
  the orchestrator plus `gen-gha`/`gha-drift` for the remaining exports. ([#40](https://github.com/OmniPotentRPC/rgpot/issues/40))

### Changed

- NWChemPot is now a pure consumer of the split [`nwchemc`](https://github.com/OmniPotentRPC/nwchemc) engine: the in-tree NWChem embed (`nwchem_c_abi.c`, `nwchem_embed_c_api.f90`, `nwchem_embed_legacy.F`) and the `-Dwith_nwchem`/`-Dnwchem_root`/`-Dnwchem_target` build options are removed. The frontend always builds and `dlopen`s `libnwchemc.so`; the capnp schema is synced byte-for-byte to nwchemc's canonical superset so a flat `NWChemParams` round-trips with no field loss.


## [1.2.0](https://github.com/OmniPotentRPC/rgpot/tree/1.2.0) - 2026-06-24

### Added

- XTBPot: GFN tight-binding via the xtb C API (GFNFF, GFN0/1/2-xTB). Feature-gated with ``-Dwith_xtb=true``. RPC names ``XTB``, ``GFNFF``, ``GFN0xTB``, ``GFN1xTB``. ([#35](https://github.com/OmniPotentRPC/rgpot/issues/35))
- TBLitePot: GFN tight-binding via the tblite C API (GFN1, GFN2, IPEA1). Feature-gated with ``-Dwith_tblite=true``. ([#36](https://github.com/OmniPotentRPC/rgpot/issues/36))
- MetatomicPot: load metatomic TorchScript models directly in C++ (vesin neighbor lists, autograd forces). Feature-gated with ``-Dwith_metatomic=true``. Requires vesin 0.5+ (``VesinDevice{VesinCPU, 0}``). ([#37](https://github.com/OmniPotentRPC/rgpot/issues/37))
- Units module (``rgpot/units.hpp``): CODATA 2018 constants plus a runtime unit expression parser, with Cap'n Proto ``lengthUnit``/``energyUnit`` negotiation on the RPC boundary. ([#38](https://github.com/OmniPotentRPC/rgpot/issues/38))

### Developer

- Lockstep monorepo release via cocogitto 7 + towncrier + ``potctl`` (``potctl/``,
  not published): ``cog bump`` runs ``potctl release sync`` then towncrier then
  ``potctl release assert --require-changelog``; ``release.yml`` publishes
  ``rgpot-core`` on stable ``v*`` tags. CI ``potentials.yml`` builds xtb/tblite and
  metatomic/vesin backends.
- Release tooling in ``potctl`` (Rust workspace crate): lockstep assert
  (meson/CMake/cargo/towncrier/pixi), fail-fast CHANGELOG gate, ``release sync``
  writes all surfaces, cargo publish --locked only, RC tags skip crates.io,
  release-prepare cargo dry-run, SECURITY.md and CODEOWNERS for release surfaces.

### Fixed

- MetatomicPot: target vesin 0.5+ ``VesinDevice`` struct (``{VesinDeviceKind, device_id}``, pass ``VesinDevice{VesinCPU, 0}``) and zero-initialized ``VesinOptions`` (``sorted`` / ``algorithm``).


## [1.1.0](https://github.com/OmniPotentRPC/rgpot/tree/1.1.0) - 2026-03-29

### Added

- XTBPot: GFN tight-binding potential via xtb (GFNFF, GFN0, GFN1, GFN2). Feature-gated with ``-Dwith_xtb=true``. ([#35](https://github.com/OmniPotentRPC/rgpot/issues/35))
- TBLitePot: GFN tight-binding potential via tblite (GFN1, GFN2, IPEA1). Feature-gated with ``-Dwith_tblite=true``. ([#36](https://github.com/OmniPotentRPC/rgpot/issues/36))
- MetatomicPot: ML atomistic models via metatomic/PyTorch with autograd forces and vesin neighbor lists. Feature-gated with ``-Dwith_metatomic=true``. ([#37](https://github.com/OmniPotentRPC/rgpot/issues/37))
- Shared unit conversion header (``rgpot/units.hpp``) with CODATA 2018 physical constants for Angstrom/Bohr, Hartree/eV, and Boltzmann conversions. ([#38](https://github.com/OmniPotentRPC/rgpot/issues/38))


## [1.0.3](https://github.com/OmniPotentRPC/rgpot/tree/1.0.3) - 2026-03-01

### Fixed

- MSVC/clang-cl compatibility: use ``/W3`` instead of GCC/Clang warning flags and skip ``-lstdc++`` link arg on Windows ([#31](https://github.com/OmniPotentRPC/rgpot/issues/31))


## [1.0.2](https://github.com/OmniPotentRPC/rgpot/tree/1.0.2) - 2026-03-01

### Added

- External integration guide covering namespace collision mitigation when embedding rgpot as a subproject ([#30](https://github.com/OmniPotentRPC/rgpot/issues/30))

### Changed

- CI dependency bumps and cleanup to prevent trailing whitespace in generated headers ([#29](https://github.com/OmniPotentRPC/rgpot/issues/29))


## [1.0.0](https://github.com/OmniPotentRPC/rgpot/tree/1.0.0) - 2026-02-15

### Added

- doc(arch): add architecture guide covering layer diagram, error conventions, and how to register new potentials
- feat(build): add release CI workflow, meson subproject install guards, CMake FetchContent readiness, and package version config
- feat(cache): add a RocksDB integration
- feat(ci): add `rust_tests` job using cargo-nextest on Ubuntu and macOS with default and `--all-features` configurations
- feat(cpp): add C++ RAII wrappers (`include/rgpot/`) — `PotentialHandle`, `InputSpec`, `CalcResult`, `RpcClient`, `Error`, with full Doxygen documentation
- feat(rpc): add feature-gated Cap'n Proto RPC client and server in Rust, sharing the existing `Potentials.capnp` schema with the C++ side
- feat(rpc): initialize a C style integration to the server
- feat(rpc): initialize a server component
- feat(rust): add Rust core library (`rgpot-core/`) with `#[repr(C)]` types, callback-based potential dispatch, status codes, thread-local error handling, and auto-generated C header via cbindgen
- feat(rust): integrate DLPack tensor exchange protocol via `dlpk` crate — core types now use `DLManagedTensorVersioned*` for device-agnostic data exchange, with borrowed (non-owning) and owned tensor helpers in new `tensor` module
- test(rust): add 39 unit tests covering types, status codes, potential lifecycle, C API, null-pointer handling, error propagation, and `free_fn` invocation

### Changed

- chore(build): add Meson `with_rust_core` option, `rust-test` / `rust-test-all` pixi tasks, and `cargo-nextest` dependency

### Fixed

- fix(cpp): remove incorrect `extern "C"` trampolines from `LJPot.hpp` and `CuH2Pot.hpp` that used raw `int` returns and `void*` params; use typed `PotentialHandle::from_impl<>()` template instead


## [0.0.1](https://github.com/OmniPotentRPC/rgpot/tree/v0.0.1) - 2024-01-26

### Added

- Initial release with C++ core: Lennard-Jones and CuH2 EAM potentials.
- CRTP-based `Potential<Derived>` template with optional caching.
- Cap'n Proto RPC server and client bridge.
- Meson and CMake build systems.
- CI build matrix (Meson/CMake x Linux/macOS x RPC/Cache feature flags).
- RPC integration tests and client bridge stress tests.
