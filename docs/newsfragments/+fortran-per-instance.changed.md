Every Fortran potential (SW, EDIP, Lenosky, Tersoff, EAM Al, FeHe, CuH2,
Water-H) keeps its neighbour table and error message in a per-instance
workspace and reports `Reentrancy::PerInstance` with `perImageInstances`,
replacing `ProcessSerial`. Separate instances evaluate on separate threads,
and a NEB that keeps one instance per image no longer rebuilds one shared
table as the images alternate (220 of 240 calls rebuilt it in a 601-atom
EAM aluminium NEB, 27.5 percent of its samples). The C entry points without
a workspace remain for the CuH2 plugin and stay process-serial.
