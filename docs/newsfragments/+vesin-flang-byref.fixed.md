The vendored vesin Fortran interface builds under LLVM Flang on Windows.
It bound `vesin_neighbors` directly, which takes the device and options
structs by value; Flang cannot lower a by-value `BIND(C)` derived type on
the `x86_64-pc-windows-msvc` target and aborts the compiler outright
("not yet implemented: passing VALUE BIND(C) derived type for this
target"), so no Fortran consumer of vesin could be built there at all.
vesin gains `vesin_neighbors_byref`, which takes both structs through
pointers and forwards them by value, and the Fortran module binds to that.
Marked as an upstream candidate alongside the other local vesin patches.
