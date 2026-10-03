# UmaPot: UMA / OMol AOTInductor frontend

`UmaPot` loads a compiled AOTInductor package (`.pt2`) that
`scripts/export_uma_aoti.py` wrote from a UMA checkpoint.

vesin builds the neighbor list. The graph returns energy and forces.
Charge and spin are per-call tensors. The compiled graph does not
call fairchem at evaluate time.

## Package

One `.pt2` per composition, charge and spin. The exporter embeds the
runtime contract in the package itself
(`aot_inductor.metadata`). `UmaPot` reads it with
`AOTIModelPackageLoader::get_metadata()`; no file next to the
package is read.

| Key | Use at runtime |
| --- | --- |
| `cutoff`, `max_neighbors` | vesin neighbor list |
| `molecular_box` | re-center into this cube (0: caller's cell) |
| `batch_max` | band graph size (0 or 1: single system) |
| `pos_dtype` | `float64` switches the input dtype |
| `task_name`, `charge`, `spin` | must equal `UmaConfig` |
| `z_set`, `natoms`, `counts` | must match the input atoms |
| `label`, `shapes`, `inputs`, `outputs` | recorded |
| `model`, `torch_version`, `fairchem_version` | recorded |

`UmaConfig.cutoff` / `max_neighbors` are defaults. The embedded
values win when present.

## Contract checks

`merge_mole` folds one composition, charge and spin into the graph.
A different charge or spin evaluates another potential energy
surface without any error; a different composition aborts inside
the graph. Every force call therefore checks the config and the
input against the embedded keys and throws `rgpot::UmaContractError`
(a `std::runtime_error`; `field()` names the key) on a mismatch:

```
UmaPot: config charge is -1, the package was exported for 0
UmaPot: input counts is {1: 4, 6: 2}, the package was exported for {1: 2, 6: 2}
```

`natoms` and `counts` exist only in packages from the current
exporter. Older packages get the `z_set` check, which cannot tell
C2H2 from C2H4.

`scripts/export_baker_uma_aoti.py` walks Baker endpoints and
deduplicates by exact composition (atom count per element), charge
and spin.

## potserv

```
Uma:<model.pt2>
Uma:<model.pt2>:<task>
```

Task default is `omol`. Device default is `cpu`.

## Build

Needs torch and vesin (`-Dwith_uma=true`, pixi env that has both).
`libuma_engine.so` is the dlopen plugin for eonclient: the host
never links torch. Config is a Cap'n Proto `UmaParams` message.

Molecular graphs use a large constant box so the neighbor list
shape is stable. Do not treat that box as a periodic crystal.

Elja / some HPC loaders need the AOTI execstack rewrite in
`aoti_execstack.hpp` (`docs/newsfragments/+uma-elja-execstack.fixed.md`).
