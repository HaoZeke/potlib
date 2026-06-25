# NWChemPot

NWChem quantum-chemistry backend for rgpot via a stable **C ABI** and optional
Fortran embed (`nwchem_embed.F`).

## Architecture

| Layer | Artifact | Role |
|-------|----------|------|
| Frontend | `NWChemPot.cc` / `NWChemPot.hpp` | Always built. `dlopen`s `libnwchem_engine`. |
| C ABI | `nwchem_c_abi.h` | Stable symbols: energy/grad, set_config, version, abi_available. |
| Stub | `nwchem_c_abi_stub.c` → `libnwchem_abi_stub.a` | Always built. Returns not-available. |
| Engine (default) | `nwchem_c_abi_cli.c` → `libnwchem_engine.so` | Always built. Runs `nwchem` subprocess. |
| Engine (embed) | `nwchem_c_abi.c` + `nwchem_embed.F` | Optional when `with_nwchem` + `nwchem_root` + Fortran. |
| DynLib | `DynLib.hpp` | Portable dlopen helper for the engine. |

```
Caller / RPC server
       │
       ▼
  NWChemPot (static, always in librgpot)
       │  dlopen(RTLD_GLOBAL)
       ▼
  libnwchem_engine.so  ──►  nwchem_c_abi_cli.c  ──►  nwchem (PATH / RGPOT_NWCHEM_EXE)
       │                   or (optional) nwchem_c_abi.c + nwchem_embed.F
       └── missing engine .so → available()=false; missing nwchem exe → abi_available=false
```

## Meson options

| Option | Default | Meaning |
|--------|---------|---------|
| `with_nwchem` | `false` | Switch engine to in-process Fortran embed (needs `nwchem_root`). |
| `nwchem_root` | `''` | `NWCHEM_TOP` for embed build. |
| `nwchem_target` | `LINUX64` | NWChem target lib dir name under `lib/`. |

Frontend + CLI engine are **always** built. Put real `nwchem` on `PATH` (or set `RGPOT_NWCHEM_EXE`).

```bash
# default: CLI engine
meson setup bbdir -Dwith_rpc=true
export RGPOT_NWCHEM_ENGINE=$PWD/bbdir/CppCore/rgpot/NWChemPot/libnwchem_engine.so
export RGPOT_NWCHEM_EXE=$(command -v nwchem)   # or scripts/mock_nwchem.sh for smoke

# optional Fortran embed (full NWChem tree)
meson setup bbdir -Dwith_nwchem=true -Dnwchem_root=$NWCHEM_TOP -Dnwchem_target=LINUX64
```

## Runtime environment

| Variable | Purpose |
|----------|---------|
| `RGPOT_NWCHEM_ENGINE` | Explicit path to `libnwchem_engine.so`. |
| `RGPOT_NWCHEM_EXE` / `NWCHEM_EXECUTABLE` | Path to `nwchem` binary (CLI engine). |
| `NWCHEM_TOP` | NWChem install/source tree (embed mode / `NWChemConfig.nwchem_root`). |
| `LD_LIBRARY_PATH` | Include dir containing `libnwchem_engine.so` (and NWChem libs for embed). |

Smoke without a full NWChem install:

```bash
RGPOT_NWCHEM_ENGINE=$PWD/bbdir/CppCore/rgpot/NWChemPot/libnwchem_engine.so \
RGPOT_NWCHEM_EXE=$PWD/scripts/mock_nwchem.sh \
  ./scripts/nwchem_calc_smoke   # or compile scripts/nwchem_calc_smoke.cc against built libs
```

## Config (`NWChemConfig`)

- `basis` (default `sto-3g`)
- `theory` (default `scf`)
- `scf_type` (default `rhf`)
- `charge`, `multiplicity`
- `engine_path`, `nwchem_root`

`setConfig()` pushes parameters into the engine via `rgpot_nwchem_set_config`.

## RPC

- Schema: `NWChemParams` union arm on `PotentialConfig`, plus `configure @1` on `Potential`.
- Server CLI: `potserv <port> NWChem` (always listed; works without engine for probe/configure).
- Python: `tests/nwchem_params.py`, optional `--nwchem-smoke` on `rpc_integ.py`.

## Tests

| Test | Suite | Notes |
|------|-------|-------|
| `NWChemPotTest` | `nwchem` | Skips if engine not loaded. |
| `NWChemCapnpMapTest` | `nwchem` | Schema mapping (requires RPC). |
| `test_nwchem_abi` | — | Links stub; checks `abi_available()==0`. |

```bash
meson test -C bbdir --suite nwchem
```

## Units

Engine returns Hartree and Hartree/Bohr gradient; frontend converts to eV and eV/Å using `rgpot::units` (same as XTB).
