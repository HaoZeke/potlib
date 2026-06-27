# CPMDPot: pure consumer of the split cpmdc engine.

cpmd_inc = include_directories('.')

cpmd_abi_stub = static_library(
    'cpmd_abi_stub',
    'cpmd_c_abi_stub.c',
    include_directories: cpmd_inc,
    install: false,
)

cpmd_frontend_cpp_args = ['-DRGPOT_HAS_CPMD_FRONTEND=TRUE']

cpmd_dl_dep = dependency('dl', required: false)
if not cpmd_dl_dep.found()
    cpmd_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)
endif

cpmdpot = static_library(
    'cpmdpot',
    'CPMDPot.cc',
    dependencies: _deps + [cpmd_dl_dep, ptlrpc_dep],
    cpp_args: _args + cpmd_frontend_cpp_args,
    include_directories: _incdirs + [cpmd_inc],
    install: not meson.is_subproject(),
)

cpmdpot_dep = declare_dependency(
    link_with: [cpmdpot],
    dependencies: [cpmd_dl_dep, ptlrpc_dep],
    include_directories: _incdirs + [cpmd_inc],
    compile_args: cpmd_frontend_cpp_args,
)

cpmd_abi_stub_dep = declare_dependency(
    link_with: cpmd_abi_stub,
    include_directories: cpmd_inc,
)
