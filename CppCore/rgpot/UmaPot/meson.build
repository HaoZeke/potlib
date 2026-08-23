# UMA / OMol: vesin neighbor list + AOTInductor .pt2 (needs torch from _deps).
uma = static_library(
    'uma',
    'UmaPot.cc',
    dependencies: _deps + [vesin_dep],
    cpp_args: _args,
    include_directories: ['../../'],
    pic: true,
    install: false,
)

# Portable engine plugin behind the generic rgpot engine C ABI
# (engine_c_abi.h): dlopen'd by hosts (eonclient), never linked, so the
# host carries no torch NEEDED entries. RUNPATH mirrors the metatomic
# engine; both reuse the variables MetatomicPot/meson.build minted.
uma_engine = library(
    'uma_engine',
    [
        'UmaPot.cc',
        'UmaEngineAbi.cc',
    ],
    dependencies: _deps + [dl_dep, vesin_dep],
    cpp_args: _args + ['-DRGPOT_ENGINE_BUILD'],
    include_directories: ['../../'],
    install: not meson.is_subproject(),
    build_rpath: _engine_build_rpath,
    install_rpath: _engine_install_rpath,
    gnu_symbol_visibility: 'default',
)

uma_engine_dep = declare_dependency(
    link_with: [uma_engine],
    include_directories: ['../../'],
)
