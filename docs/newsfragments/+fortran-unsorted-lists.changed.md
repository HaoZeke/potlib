The Fortran potentials ask vesin for unsorted neighbour lists and order each
atom's row by partner index with two linear counting sorts, which replaces
vesin's global sort (10.6 percent of an EAM aluminium NEB) and keeps every
force sum in one order whether vesin reused its cached topology or rebuilt it.
