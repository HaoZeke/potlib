# Fortran-backed potentials, ported to Fortran 2018.
#
# Sources are listed in dependency order for compilers without automatic
# module ordering; meson's Fortran scanner reorders as needed.
#
# Only the bind(c) entry points need to leave the enclosing librgpot, and
# C++ callers reach them from inside the same shared object, so the whole
# static library builds with hidden visibility: no bare Fortran symbol
# (or legacy COMMON block name) ever reaches the dynamic table.

_fpot_args = fc.get_supported_arguments(
    [
        '-std=f2018',
        '-fimplicit-none',
        '-Wall',
        '-Wno-unused-dummy-argument',
        '-fvisibility=hidden',
    ],
)

fortran_pots_sources = files(
    'rgpot_kinds.f90',
    'rgpot_ferror.f90',
    'rgpot_neighbors.f90',
    'rgpot_sw.f90',
    'rgpot_sw_capi.f90',
    'rgpot_cuh2.f90',
    'rgpot_cuh2_capi.f90',
)

# C++ faces over the bind(c) entries.
fortran_pots_cpp_sources = files('FortranPots.cc')

fortran_pots = static_library(
    'rgpot_fortran_pots',
    fortran_pots_sources + fortran_pots_cpp_sources,
    dependencies: [vesin_f_dep] + _deps,
    fortran_args: _fpot_args,
    cpp_args: _args,
    include_directories: _incdirs,
    pic: true,
    install: false,
)

fortran_pots_dep = declare_dependency(
    link_with: fortran_pots,
    dependencies: [vesin_f_dep],
)

# Physics checks that run without the C++ stack: a failure here points at
# the kernel, not the bindings.
if get_option('with_tests')
    test_sw_gather = executable(
        'test_sw_gather',
        'test_sw_gather.f90',
        dependencies: [fortran_pots_dep],
        fortran_args: _fpot_args,
        install: false,
    )
    test('FortranSWGather', test_sw_gather, timeout: 120)

    test_cuh2_gather = executable(
        'test_cuh2_gather',
        'test_cuh2.f90',
        dependencies: [fortran_pots_dep],
        fortran_args: _fpot_args,
        install: false,
    )
    test('FortranCuH2Gather', test_cuh2_gather, timeout: 120)
endif
