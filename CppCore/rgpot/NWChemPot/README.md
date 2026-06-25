# NWChemPot

NWChem quantum-chemistry backend for rgpot via a stable **C ABI** and optional
Fortran embed (`nwchem_embed.F`).

## Architecture

| Layer | Artifact | Role |
|-------|----------|------|
| Frontend | `NWChemPot.cc` / `NWChemPot.hpp` | Always built. `dlopen`s `libnwchem_engine`. |
| C ABI | `nwchem_c_abi.h` | Stable symbols: energy/grad, set_config, version, abi_available. |
| Stub | `nwchem_c_abi_stub.c` → `libnwchem_abi_stub.a` | Always built. Returns not-available. |
| Engine | `nwchem_c_abi.c` + `nwchem_embed.F` → `libnwchem_engine.so` | Optional (`-Dwith_nwchem=true`). |
| DynLib | `DynLib.hpp` | Same portable dlopen pattern as Psi4Pot. |

```
Caller / RPC server
       │
       ▼
  NWChemPot (static, always in librgpot)
       │  dlopen(RTLD_GLOBAL)
       ▼
  libnwchem_engine.so  ──►  nwchem_c_abi.c  ──►  nwchem_embed.F  ──►  NWChem libs
       │
       └── or missing / stub only → available()=false, force throws
```

## Meson options

| Option | Default | Meaning |
|--------|---------|---------|
| `with_nwchem` | `false` | Build optional `libnwchem_engine` when root/target set. |
| `nwchem_root` | `''` | `NWCHEM_TOP` install / source root. |
| `nwchem_target` | `LINUX64` | NWChem target lib dir name under `lib/`. |

Frontend is **always** compiled regardless of `with_nwchem` (unlike Psi4 which is gated by `with_psi4`). Engine path is optional.

```bash
meson setup bbdir -Dwith_nwchem=true -Dnwchem_root=$NWCHEM_TOP -Dnwchem_target=LINUX64
```

## Runtime environment

| Variable | Purpose |
|----------|---------|
| `RGPOT_NWCHEM_ENGINE` | Explicit path to `libnwchem_engine.so`. |
| `NWCHEM_TOP` | NWChem install/source tree (also settable via `NWChemConfig.nwchem_root`). |
| `LD_LIBRARY_PATH` | Must include NWChem runtime libs when using a real engine. |

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

Engine returns Hartree and Hartree/Bohr gradient; frontend converts to eV and eV/Å using `rgpot::units` (same as Psi4Pot / XTB).
