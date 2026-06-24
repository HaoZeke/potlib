project('fortcuh2', 'fortran',
  version : '0.0.2',
  default_options : ['warning_level=3',
                     'debug=true',
                     'buildtype=debug'])

_fargs = []
_deps = []
_linkto = []
_incdirs = []

fc = meson.get_compiler('fortran')
host_system = host_machine.system()
is_windows = host_system == 'windows'

_fargs += fc.get_supported_arguments([
  '-Wno-return-type',
  '-Wno-unused-variable',
  '-fimplicit-none',
])

# Darwin: a gfortran-only dylib misses libc/libSystem (_exp, _memcpy, _memset).
# Even with the C driver, shared-lib links are flaky on conda macOS runners.
# Build static on Darwin so the final C++/C executable resolves system symbols.
if host_system == 'darwin'
  cuh2 = static_library('fortcuh2',
                        'eam_dat.f90',
                        'eamroutines.f90',
                        'eam_isoc.f90',
                        dependencies: _deps,
                        fortran_args : _fargs,
                        install : true)
else
  cuh2 = library('fortcuh2',
                 'eam_dat.f90',
                 'eamroutines.f90',
                 'eam_isoc.f90',
                 dependencies: _deps,
                 fortran_args : _fargs,
                 install : true)
endif
_linkto += cuh2

fortcuh2_dep = declare_dependency(include_directories: _incdirs,
                                  link_with: _linkto,
                                  dependencies: _deps)

if (not meson.is_subproject())
  cuh2exec = executable('tmp_run', 'main.f90',
                       dependencies: fortcuh2_dep,
                       fortran_args: _fargs)
endif
