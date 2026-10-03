`librgpot` no longer links MPI. With MPI found, the build adds `librgpot_mpi`
(meson dependency and pkg-config name `rgpot-mpi`) holding every MPI call of the
calculator groups, and `bindCalculators` or `calculatorsUseMpi` load it with
`dlopen` on first use through one C entry point, `rgpot_mpi_api_v1`
(`rgpot/calculator_mpi_abi.h`). A host that never asks for calculator groups no
longer maps libmpi, whose UCX load-time hooks cost 0.4 s per process start.
`loadCalculatorMpi`, `calculatorMpiLoaded` and `calculatorMpiLoadError` let a
host load it from a chosen path (or `RGPOT_MPI_LIBRARY`) and report failures.
