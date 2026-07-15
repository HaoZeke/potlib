# Python package: rgpot (pip-installable core)

py_mod = import('python')
py = py_mod.find_installation(pure: false)
py_dep = py.dependency()

# pybind11 from pkg-config or Python package (meson-python build env)
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

_py_incs = _incdirs
# numpy headers (optional for array_t forcecast)
_numpy_inc = run_command(
  py,
  '-c',
  'import numpy; print(numpy.get_include())',
  check: false,
)
if _numpy_inc.returncode() == 0
  _py_incs += include_directories(_numpy_inc.stdout().strip(), is_system: true)
endif

_py_rpath = '$ORIGIN'
if host_machine.system() != 'windows'
  # meson-python places shared libs under .rgpot.mesonpy.libs next to package
  _py_rpath = '$ORIGIN:$ORIGIN/../.rgpot.mesonpy.libs'
endif

# Link LJ pot explicitly so LJPot symbols resolve (not only via librgpot NEEDED).
_py_link = [rgpot, lennard_jones]
if is_variable('cuh2')
  _py_link += [cuh2]
endif

py.extension_module(
  '_core',
  sources: files('bind/bind_module.cpp'),
  dependencies: [py_dep, pybind11_dep] + _deps,
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
