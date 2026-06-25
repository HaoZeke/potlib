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
    NWChemConfig (internal) → RgpotNWChemParams (embed buffers only)
            │
            ▼
    libnwchem_engine.so / nwchem_embed.F
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

## Options = `NWChemParams` (Cap'n Proto)

| field (schema) | meaning |
|----------------|---------|
| `basis` | Gaussian basis |
| `theory` | `scf` / `dft` / `blyp` / `b3lyp` / … |
| `scfType` | HF `rhf`/`uhf`, or DFT XC when theory is dft/blyp* |
| `charge`, `multiplicity` | charge, 2S+1 |
| `enginePath` | `libnwchem_engine.so` (frontend dlopen) |
| `nwchemRoot` | `NWCHEM_TOP` (frontend env for embed) |

C++ in-process (same struct as RPC):

```cpp
::capnp::MallocMessageBuilder msg;
auto p = msg.initRoot<::NWChemParams>();
p.setTheory("dft");
p.setScfType("blyp");
p.setEnginePath("/path/to/libnwchem_engine.so");
rgpot::NWChemPot pot(p.asReader());  // setParams from Cap'n Proto only
// pot(pos, Z, box) internally: NWChemParams -> embed C buffers
```

Python/RPC: `configure_nwchem(pot, pot_capnp, theory="dft", scf_type="blyp", ...)`.

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
