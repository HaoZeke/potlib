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
