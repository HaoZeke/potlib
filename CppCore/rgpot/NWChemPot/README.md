# NWChemPot - backend under rgpot `PotentialConfig` params

## User options = rgpot `PotentialConfig` (Cap'n Proto)

rgpot has **one** user-facing parameter carrier: `PotentialConfig` in
`Potentials.capnp`, an extensible **union** of backend-specific option structs.
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
    PotentialConfig  { nwchem = NWChemParams{...} }   rgpot params (in/out)
            │
            ├── RPC:  configure(config)
            └── C++:  setPotentialConfig(config)  or  setParams(nwchem only)
            │
            ▼  (nwchem arm only on this pot)
    serialized flat Cap'n Proto NWChemParams bytes
            │  nwchemc_set_params / nwchemc_energy_gradient
            ▼
    libnwchemc.so (or transitional in-tree libnwchem_engine.so)
      C parser + nwchem_embed_c_api.f90 (bind(C), iso_c_binding)
      -> nwchem_embed_legacy.F (geom/basis via nw_inp_from_character embed API;
        rtdb/task_energy/task_gradient; no user .nw / subprocess CLI)
```

Geometry for `calculate` stays on `ForceInput`; `PotentialConfig` is method/backend setup only.

**Not** a subprocess `nwchem` CLI. Embed still needs `NWCHEM_TOP` + built
`libnwchemc.so` / transitional `libnwchem_engine.so`
(`-Dwith_nwchem=true -Dnwchem_root=...`).

## Layers

| Piece | Built when | Role |
|-------|------------|------|
| `NWChemPot.cc` frontend | always | Serialize `NWChemParams`, `dlopen` engine, units to eV/Angstrom |
| `nwchem_c_abi.h` | header | stable C symbols (`nwchemc_*`) |
| `nwchem_c_abi_stub.c` | always | `nwchemc_available()==0`, compute fails |
| `nwchem_c_abi.c` + `nwchem_embed_c_api.f90` + `nwchem_embed_legacy.F` | `-Dwith_nwchem=true` `-Dnwchem_root=...` | real embed -> runtime-loaded engine |

```
app / potserv
    │
    ▼
NWChemPot (static, always in librgpot)
    │  dlopen(RTLD_GLOBAL)  NWCHEMC_LIBRARY / RGPOT_NWCHEMC_ENGINE / enginePath
    ▼
libnwchemc.so
    nwchemc_set_params / nwchemc_energy_gradient / nwchemc_available
    │
    ▼
nwchem_embed_legacy.F  ->  geom/basis via embed API + task_energy/gradient  (NWCHEM_TOP libs)
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
| `NWCHEMC_LIBRARY` | Path to `libnwchemc.so` |
| `RGPOT_NWCHEMC_ENGINE` | Path to `libnwchemc.so` |
| `RGPOT_NWCHEM_ENGINE` | Transitional path to `libnwchem_engine.so` |
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
| `enginePath` | `""` | Frontend: explicit `libnwchemc.so` or transitional `libnwchem_engine.so`; empty -> env/probe |
| `nwchemRoot` | `""` | Frontend: `NWCHEM_TOP`; empty -> env |

Defaults are the Cap'n Proto schema defaults.

### DFT: two equivalent forms

1. **Preferred:** `theory="dft"`, `scfType="blyp"` (or `b3lyp`, ...): explicit DFT + XC.
2. **Shorthand:** `theory="blyp"`: embed maps theory alias to `dft` + XC; still fine if `scfType` left default.

HF: `theory="scf"`, `scfType="rhf"` or `"uhf"`.

### Lifecycle (apply vs calculate)

| Step | What happens |
|------|----------------|
| `setPotentialConfig` / RPC `configure` / `setParams` | Sticky on the C++ pot: stores serialized flat Cap'n Proto `NWChemParams` words |
| Each `forceImpl` / `calculate` | Frontend passes the current message bytes to `nwchemc_energy_gradient(...)` at the dlopen boundary |
| Direct C callers | Pass the same unpacked flat `NWChemParams` message bytes to `nwchemc_set_params(...)` or `nwchemc_energy_gradient(...)` |

### Units

Embed / C ABI: energy **Hartree**, gradient **Hartree/Bohr**. Frontend converts to rgpot **eV** / **eV/Angstrom**. Geometry units for `calculate` remain on `ForceInput`, not on `PotentialConfig`.

```cpp
::capnp::MallocMessageBuilder msg;
auto cfg = msg.initRoot<::PotentialConfig>();
auto nw = cfg.initNwchem();
nw.setTheory("dft");
nw.setScfType("blyp");
nw.setEnginePath("/path/to/libnwchemc.so");
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

ABI: Hartree, Hartree/Bohr. Frontend: eV, eV/Angstrom (`rgpot::units`).
