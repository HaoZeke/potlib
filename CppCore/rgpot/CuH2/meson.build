# gfortran runtime for C++ links that pull in static fortcuh2 (needed when the
# parent drives the link with clang++/g++ rather than gfortran).
_cuh2_extra = [fortcuh2_dep]
_gfortran = meson.get_compiler('cpp').find_library('gfortran', required: false)
if not _gfortran.found()
    # Fortran is enabled at project level when has_fortran is true (this dir).
    _gfortran = meson.get_compiler('fortran').find_library(
        'gfortran',
        required: false,
    )
endif
if _gfortran.found()
    _cuh2_extra += [_gfortran]
endif

# Static on Darwin so system clang++ resolves libc/libSystem when the final
# executable links; keeps fortcuh2 out of global _deps (see top-level meson).
if host_machine.system() == 'darwin'
    cuh2 = static_library(
        'cuh2',
        'CuH2Pot.cc',
        'cuh2Utils.cc',
        dependencies: _deps + _cuh2_extra,
        cpp_args: _args,
        include_directories: _incdirs,
        install: false,
    )
else
    cuh2 = library(
        'cuh2',
        'CuH2Pot.cc',
        'cuh2Utils.cc',
        # fortcuh2 is scoped here (not in global _deps) so LJ/ptlrpc dylibs on
        # Darwin are not polluted with gfortran link flags from the Fortran dep.
        dependencies: _deps + _cuh2_extra,
        cpp_args: _args,
        include_directories: _incdirs,
        install: not meson.is_subproject(),
    )
endif
