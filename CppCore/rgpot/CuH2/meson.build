# Slab-geometry helpers for CuH2 systems; the potential itself is a Fortran
# 2018 kernel under rgpot/fortran. Static convenience lib; ships inside the
# librgpot umbrella (link_whole).
cuh2 = static_library(
    'cuh2',
    'cuh2Utils.cc',
    dependencies: _deps,
    cpp_args: _args,
    include_directories: _incdirs,
    pic: true,
    install: false,
)
