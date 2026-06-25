# NWChemPot — backend under rgpot `PotentialConfig` params

## User options = rgpot `PotentialConfig` (Cap'n Proto)

rgpot has **one** user-facing parameter carrier: `PotentialConfig` in
`Potentials.capnp` — an extensible **union** of backend-specific option structs.
`NWChemParams` is only the **nwchem arm**, not a separate config ecosystem.

Same schema for RPC and in-process; add future arms without new TOML/JSON:

| Arm (today / planned) | Payload | Backend |
|----------------------|---------|---------|
| `none` | void | no backend knobs / no-op configure |
| `nwchem` | `NWChemParams` | NWChemPot |
| *(later)* `metatomic` | `MetatomicParams` | MetatomicPot |
| *(later)* `xtb` / `tblite` | … | XTBPot / TBLitePot |

```
user / client
    PotentialConfig  { nwchem = NWChemParams{...} }   ← rgpot params (in/out)
            │
            ├── RPC:  configure(config)
            └── C++:  setPotentialConfig(config)  or  setParams(nwchem only)
            │
            ▼  (nwchem arm only on this pot)
    RgpotNWChemParams (embed C buffers only — fixed at .so boundary)
            │  rgpot_nwchem_set_params / energy_grad
            ▼
    libnwchem_engine / static embed
      nwchem_c_abi.c → nwchem_embed_c_api.f90 (bind(C), iso_c_binding)
      → nwchem_embed_legacy.F (geom/bas/rtdb/task_* only; no input files)
```

Geometry for `calculate` stays on `ForceInput`; `PotentialConfig` is method/backend setup only.

**Not** a subprocess `nwchem` CLI. Embed still needs `NWCHEM_TOP` + built
`libnwchem_engine.so` (`-Dwith_nwchem=true -Dnwchem_root=...`).

## Layers

| Piece | Built when | Role |
|-------|------------|------|
| `NWChemPot.cc` frontend | always | `dlopen` engine, units to eV/Å |
| `nwchem_c_abi.h` | header | stable symbols (`rgpot_nwchem_*`) |
| `nwchem_c_abi_stub.c` | always | `abi_available()==0`, compute fails |
| `nwchem_c_abi.c` + `nwchem_embed_c_api.f90` + `nwchem_embed_legacy.F` | `-Dwith_nwchem=true` `-Dnwchem_root=...` | real embed → static/shared engine |

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
| `NWCHEM_TOP` | Hint for engine/data paths (optional; also `NWChemParams.nwchemRoot`) |
| `LD_LIBRARY_PATH` | NWChem `lib/<target>` (and deps) when embed was linked with unresolved symbols |

## `NWChemParams` fields (payload inside `PotentialConfig.nwchem`)

| field | default | meaning |
|-------|---------|---------|
| `basis` | `sto-3g` | Gaussian basis |
| `theory` | `scf` | Method: `scf`, `dft`, `blyp`, `b3lyp`, … |
| `scfType` | `rhf` | HF: `rhf`/`uhf`; with DFT: XC functional (`blyp`, …) |
| `charge` | `0` | Molecular charge |
| `multiplicity` | `1` | 2S+1 |
| `enginePath` | `""` | Frontend: `libnwchem_engine.so` (dlopen); empty → env/probe |
| `nwchemRoot` | `""` | Frontend: `NWCHEM_TOP`; empty → env |

Defaults match schema + embed helper `rgpot_nwchem_params_default` (internal only).

### DFT: two equivalent forms

1. **Preferred:** `theory="dft"`, `scfType="blyp"` (or `b3lyp`, …) — explicit DFT + XC.
2. **Shorthand:** `theory="blyp"` (embed maps theory alias → `dft` + XC); still fine if `scfType` left default.

HF: `theory="scf"`, `scfType="rhf"` or `"uhf"`.

### Lifecycle (apply vs calculate)

| Step | What happens |
|------|----------------|
| `setPotentialConfig` / RPC `configure` / `setParams` | Sticky on the C++ pot: stores internal mirror until next configure |
| Each `forceImpl` / `calculate` | Frontend copies full current options into embed buffers and calls `rgpot_nwchem_energy_grad(..., &params, ...)` (per-call at embed boundary) |
| Embed only: `set_params` then `energy_grad(..., NULL, ...)` | Sticky inside engine `.so` via last `g_params` (advanced; normal path is through the pot) |

### Units

Embed / C ABI: energy **Hartree**, gradient **Hartree/Bohr**. Frontend converts to rgpot **eV** / **eV/Å**. Geometry units for `calculate` remain on `ForceInput`, not on `PotentialConfig`.

```cpp
::capnp::MallocMessageBuilder msg;
auto cfg = msg.initRoot<::PotentialConfig>();
auto nw = cfg.initNwchem();
nw.setTheory("dft");
nw.setScfType("blyp");
nw.setEnginePath("/path/to/libnwchem_engine.so");
rgpot::NWChemPot pot;
pot.setPotentialConfig(cfg.asReader());  // rgpot params, nwchem arm
```

Python: `configure_nwchem` builds `PotentialConfig` with `nwchem` set.

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
