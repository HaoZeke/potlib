# NWChemPot — stable C ABI, runtime-loaded engine (no CLI)

In-process NWChem via a **stable C ABI** (`nwchem_c_abi.h`), loaded at runtime
with `dlopen` (`DynLib.hpp`), same *optional backend* idea as other rgpot pots.

There is **no subprocess / `nwchem` CLI driver**. The only real backend is the
Fortran embed (`nwchem_embed.F`) compiled into `libnwchem_engine.so` against a
full `NWCHEM_TOP` tree.

## Layers

| Piece | Built when | Role |
|-------|------------|------|
| `NWChemPot.cc` frontend | always | `dlopen` engine, units to eV/Å |
| `nwchem_c_abi.h` | header | stable symbols (`rgpot_nwchem_*`) |
| `nwchem_c_abi_stub.c` | always | `abi_available()==0`, compute fails |
| `nwchem_c_abi.c` + `nwchem_embed.F` | `-Dwith_nwchem=true` `-Dnwchem_root=...` | real embed → `libnwchem_engine.so` |

```
app / potserv
    │
    ▼
NWChemPot (static, always in librgpot)
    │  dlopen(RTLD_GLOBAL)  RGPOT_NWCHEM_ENGINE / libnwchem_engine.so
    ▼
libnwchem_engine.so
    rgpot_nwchem_energy_grad / set_config / abi_available
    │
    ▼
nwchem_embed.F  →  rtdb / task_energy / task_gradient  (NWCHEM_TOP libs)
```

## Meson

```bash
# Frontend only (CI default): stub, no engine .so unless you build embed separately
meson setup bbdir -Dwith_rpc=false

# Real engine: need NWChem source/install with src/include and lib/<target>/
export NWCHEM_TOP=/path/to/nwchem   # clone + build once; see scripts/setup_nwchem_embed.sh
meson setup bbdir_nwc \
  -Dwith_nwchem=true \
  -Dnwchem_root=$NWCHEM_TOP \
  -Dnwchem_target=LINUX64
meson compile -C bbdir_nwc
# => bbdir_nwc/CppCore/rgpot/NWChemPot/libnwchem_engine.so
```

conda/pixi `nwchem` packages ship the **driver binary**, not embed headers — they
do **not** satisfy `nwchem_root`. Use a source tree (clone) for embed builds.

## Runtime

| Variable | Purpose |
|----------|---------|
| `RGPOT_NWCHEM_ENGINE` | Path to `libnwchem_engine.so` |
| `NWCHEM_TOP` | Hint for engine/data paths (optional; also `NWChemConfig.nwchem_root`) |
| `LD_LIBRARY_PATH` | NWChem `lib/<target>` (and deps) when embed was linked with unresolved symbols |

## Config / theory (ABI strings)

`rgpot_nwchem_set_config(basis, theory, scf_type, charge, mult)` and the same
three strings on `rgpot_nwchem_energy_grad(...)`.

| `theory` | `scf_type` | NWChem side |
|----------|------------|-------------|
| `scf` (default) | `rhf` / `uhf` | HF SCF |
| `dft` | `blyp`, `b3lyp`, … | DFT + `dft:xc` |
| `blyp` / `b3lyp` | (optional override) | mapped to `dft` + XC |

`NWChemConfig` in C++ mirrors these fields.

## Direct C++ smoke (no RPC)

```bash
scripts/setup_nwchem_embed.sh clone   # third_party/nwchem (shallow)
# build NWChem once per their docs, then:
scripts/setup_nwchem_embed.sh configure   # meson with nwchem_root
scripts/setup_nwchem_embed.sh calc        # water SCF/BLYP via NWChemPot only
```

## RPC (optional, separate)

`potserv <port> NWChem` + `configure` with `NWChemParams` is optional plumbing on
top of the same frontend; it is not required for the C ABI or embed path.

## Units

ABI: Hartree, Hartree/Bohr. Frontend: eV, eV/Å (`rgpot::units`).
