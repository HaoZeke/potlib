# Linked UMA / OMol frontend. Same torch/metatomic link as MetatomicPot.
uma = static_library(
    'uma',
    'UmaPot.cc',
    dependencies: _deps,
    cpp_args: _args + ['-DRGPOT_METATOMIC_LINKED=1'],
    include_directories: ['../../'],
    pic: true,
    install: false,
)
