Eight potentials absorbed from eOn arrive as Fortran 2018 kernels under
`CppCore/rgpot/fortran/`: Stillinger-Weber, EDIP, Lenosky, Tersoff, EAM
aluminium, FeHe, CuH2, and TIP4P-H. Each is a rewrite rather than a
wrapper -- modules with `implicit none`, derived-type parameters in place
of COMMON blocks, kinds from `iso_fortran_env` asserted against the C
types at compile time, `intent` on every argument, `pure` kernels,
structured control flow, and status returns instead of `stop`.

Neighbours come from vesin through `rgpot_neighbors`, a CSR full-list
table carrying pair vectors and distances, and the vendored vesin Fortran
interface gained the C API's Verlet `skin` option so the kernels keep the
list caching their predecessors had. Pair sums are restated as gathers:
each atom accumulates the whole force acting on it and writes one column,
so the atom loops run under `do concurrent`. Bond-order and embedding
kernels reach the same form in three passes, since the environment
derivative is not known until its sum completes.

Each kernel carries a Fortran-only test (translation invariance, zero net
force, analytic forces against central differences of the energy) and is
pinned in `FortranPotsTest` to the reference energies of the kernels it
replaces. The kernel archives link under `--exclude-libs`, so no Fortran
symbol reaches the library interface; a test fails the suite if one ever
does.
