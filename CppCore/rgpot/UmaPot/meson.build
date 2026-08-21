# Persistent fairchem UMA / OMol frontend. No torch C++ link.
_uma_helper = meson.current_source_dir() / 'uma_helper.py'
_uma_helper_installed = get_option('prefix') / get_option('datadir') / 'rgpot' / 'uma_helper.py'

uma = static_library(
    'uma',
    'UmaPot.cc',
    dependencies: _deps,
    cpp_args: _args + [
        '-DRGPOT_UMA_HELPER_SOURCE="@0@"'.format(_uma_helper),
        '-DRGPOT_UMA_HELPER_INSTALLED="@0@"'.format(_uma_helper_installed),
    ],
    include_directories: ['../../'],
    pic: true,
    install: false,
)

install_data(
    'uma_helper.py',
    install_dir: get_option('datadir') / 'rgpot',
)
