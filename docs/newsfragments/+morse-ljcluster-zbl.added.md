Three classical potentials absorbed from eOn, always built: `MorsePot`
(pairwise Morse with the shifted cutoff, platinum defaults through
`MorseConfig`), `LJClusterPot` (12-6 Lennard-Jones on free boundaries,
`LJClusterConfig`), and `ZBLPot` (screened nuclear repulsion with the
LAMMPS switching function, `ZBLConfig{cut_inner, cut_global}`, which
rejects a cutoff pair outside `0 < cut_inner < cut_global`). All three
take their pairs from the shared `PairListCache`, carry `paramsKey()`
fingerprints, and appear in `pot_bench`. `PotType` gains `Morse`,
`LJCluster` and `ZBL`.
