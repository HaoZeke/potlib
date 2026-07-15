# Python package: rgpot (LJ + MetatomicDlopen frontend; engine when built)

py_mod = import('python')
py = py_mod.find_installation(pure: false)
py_dep = py.dependency()

pybind11_dep = dependency('pybind11', required: false)
if not pybind11_dep.found()
  _pyb11_inc = run_command(
    py,
    '-c',
    'import pybind11; print(pybind11.get_include())',
    check: true,
  ).stdout().strip()
  pybind11_dep = declare_dependency(
    include_directories: include_directories(_pyb11_inc, is_system: true),
  )
endif

if not is_variable('rgpot')
  error('with_python requires the full rgpot library (disable with_rpc_client_only)')
endif
if not is_variable('metatomic_dlopen')
  error('with_python requires MetatomicDlopen (rgpot/MetatomicPot)')
endif

_py_incs = _incdirs
_numpy_inc = run_command(
  py,
  '-c',
  'import numpy; print(numpy.get_include())',
  check: false,
)
if _numpy_inc.returncode() == 0
  _py_incs += include_directories(_numpy_inc.stdout().strip(), is_system: true)
endif

_py_rpath = '$ORIGIN:$ORIGIN/../.rgpot.mesonpy.libs:$ORIGIN/lib'

_py_link = [rgpot, lennard_jones, metatomic_dlopen]
if is_variable('cuh2')
  _py_link += [cuh2]
endif

dl_dep = dependency('dl', required: false)
if not dl_dep.found()
  dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)
endif

py.extension_module(
  '_core',
  sources: files('bind/bind_module.cpp'),
  dependencies: [py_dep, pybind11_dep, dl_dep] + _deps,
  include_directories: _py_incs,
  link_with: _py_link,
  cpp_args: _args,
  install: true,
  install_rpath: _py_rpath,
  build_rpath: _py_rpath,
  subdir: 'rgpot',
)

py.install_sources(
  files('rgpot/__init__.py'),
  pure: false,
  subdir: 'rgpot',
)

# Install portable engine plugin next to package when built
if is_variable('metatomic_engine')
  # meson-python already installs library targets with install: true into
  # .rgpot.mesonpy.libs; also expose a stable rgpot/lib/ path via custom install
  # of a copy is hard — document mesonpy.libs + default_metatomic_engine_path().
  message('metatomic_engine will install into the wheel (portable plugin)')
endif
