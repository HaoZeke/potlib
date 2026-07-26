Four link-layout defects in the umbrella build, each of which only
surfaced in a profile that combines the RPC stack with the Fortran
potentials:

- Every convenience archive carried its own copy of the vendored vesin
  translation unit, because meson copies a static library's objects into
  each static library that links it, so the umbrella saw multiple
  definitions of vesin's thread-local error state. The archives take a
  headers-only view now and the objects enter `librgpot` -- and each leaf
  binary -- exactly once.
- `librgpot` linked without the Fortran runtime: it is a C++ target that
  takes the kernels through `link_whole`, which leaves meson no Fortran
  source to infer the runtime from. It is now named per compiler id.
- `ptlrpc_dep` exported the generated capnp `.cpp` as a dependency source,
  compiling a second copy of that translation unit into every consumer.
  Only the generated header propagates.
- The umbrella exported the RPC entry points only when something inside
  `librgpot` happened to reference them; they are folded in with
  `link_whole` now, since it is consumers that call them.
