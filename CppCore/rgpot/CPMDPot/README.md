# CPMDPot - backend under rgpot `PotentialConfig` params

## User options = rgpot `PotentialConfig` (Cap'n Proto)

rgpot has one user-facing parameter carrier: `PotentialConfig` in
`CppCore/rgpot/rpc/Potentials.capnp`. `CPMDParams` is the `cpmd` arm payload.
Geometry stays on `ForceInput`; `PotentialConfig` is method/backend setup only.

| arm | payload | backend |
| --- | --- | --- |
| `none` | `Void` | no-op configure |
| `nwchem` | `NWChemParams` | NWChemPot |
| `cpmd` | `CPMDParams` | CPMDPot |

## Runtime model

`CPMDPot.cc` is an always-built frontend that serializes `CPMDParams`, `dlopen`s
the split `libcpmdc` engine, and calls the stable C ABI in `cpmd_c_abi.h`.
rgpot is a pure consumer of the engine implementation.
The local ABI header also mirrors `cpmdc_feature_count`,
`cpmdc_feature_table`, and `cpmdc_feature_find` so frontends can discover
engine/catalog support from the same C boundary.

Engine lookup order:

| variable/path | role |
| --- | --- |
| `CPMDParams.enginePath` | explicit frontend library path |
| `CPMDC_LIBRARY` | explicit frontend library path |
| `RGPOT_CPMDC_ENGINE` | explicit frontend library path |
| `RGPOT_CPMD_ENGINE` | explicit frontend library path |
| `libcpmdc.so`, `./libcpmdc.so`, `libcpmdc.dylib`, `./libcpmdc.dylib`, `cpmdc.dll` | probe names |

`CPMDParams.cpmdRoot` is applied as `CPMD_ROOT` before the engine is loaded.

## `CPMDParams` fields

| field | default | role |
| --- | --- | --- |
| `functional` | `BLYP` | DFT functional default |
| `cutOffRy` | `70.0` | plane-wave cutoff in Ry |
| `charge` | `0` | system charge |
| `multiplicity` | `1` | spin multiplicity |
| `task` | `gradient` | method task hint |
| `title` | empty | optional CPMD title |
| `memoryMb` | `0` | frontend memory hint |
| `scratchDir` | empty | CPMD file placement hint |
| `permanentDir` | empty | preferred CPMD file placement hint |
| `cpmdRoot` | empty | `CPMD_ROOT` hint |
| `enginePath` | empty | explicit `libcpmdc` path |
| `inputBlocks` | empty | raw CPMD input blocks |
| `inputSections` | empty | typed or raw CPMD input sections |

`inputSections` supports `generic`, `system`, `cpmd`, `dft`, `atoms`, `set`, and
`raw` arms. `set` carries a `SECTION.KEYWORD` key plus optional value and maps to
the CPMD `SET` directive path in the engine.
For typed `atoms`, `pseudopotentials` are keyed by element symbol and are used by
the engine to group `ForceInput` coordinates into `&ATOMS`; every atomic number
in a geometry step must have a matching pseudopotential entry. If `atoms` is
omitted, the engine provides built-in BLYP defaults only for H and O.

## Python helpers and RPC smoke

`tests/cpmd_params.py` builds `CPMDParams` and `PotentialConfig.cpmd` messages
for pycapnp clients. Use `input_sections` for the full structured section
surface; each item has a `kind` plus arm fields, for example:

```python
params = make_cpmd_params(
    pot_capnp,
    input_sections=[
        {"kind": "generic", "name": "PIMD",
         "directives": [{"keyword": "TEMP", "args": ["300"]}]},
        {"kind": "cpmd", "molecularDynamics": True, "maxStep": 8},
        {"kind": "dft", "functional": "PBE0", "lsd": True},
        {"kind": "atoms",
         "pseudopotentials": [
             {"element": "Si", "path": "Si_MT_PBE.psp", "lmax": 2},
         ]},
        {"kind": "raw", "text": "&VDW\n  DISPERSION\n&END"},
    ],
)
```

```bash
pixi run -e rpctest python tests/test_cpmd_params.py
pixi run -e rpctest python tests/test_rpc_integ_cpmd.py
pixi run -e rpctest python tests/rpc_integ.py \
  --server-bin ./bbdir/CppCore/potserv \
  --cpmd-smoke
```

The smoke calls `configure(none)` and `configure(cpmd)`. `calculate()` requires
an available `libcpmdc` engine.
