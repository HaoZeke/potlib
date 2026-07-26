The CuH2 potential is the in-tree Fortran 2018 kernel
(`CppCore/rgpot/fortran/rgpot_cuh2.f90`), reached through
`rgpot::fortranpots::CuH2Pot` from `rgpot/fortran/FortranPots.hpp`. It
matches the energy and forces of the kernel it replaces to 1e-6 on the
geometry `CuH2PotTest` pins. `rgpot::CuH2Pot` and
`rgpot/CuH2/CuH2Pot.hpp` are gone, and with them the `fortcuh2`
subproject and its wrap-git entry, so a source build needs no download
for the Fortran pots and the offline `--wrap-mode=nodownload` CI leg
covers them. `rgpot/CuH2/cuh2Utils.hpp` keeps the slab-geometry helpers
unchanged. In-tree sources gate the Fortran pots on
`RGPOT_HAS_FORTRAN_POTS`, the same cflag `rgpot.pc` exports to
consumers.
