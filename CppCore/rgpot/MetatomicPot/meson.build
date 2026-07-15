# Metatomic: thin dlopen frontend (always) + linked pot + engine plugin (with_metatomic)

mta_inc = include_directories('.')

dl_dep = dependency('dl', required: false)
if not dl_dep.found()
    dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)
endif

# Thin frontend — no torch; safe for pip host without metatomic at link time
metatomic_dlopen = static_library(
    'metatomic_dlopen',
    'MetatomicDlopen.cc',
    dependencies: _deps + [dl_dep],
    cpp_args: _args,
    include_directories: _incdirs + [mta_inc],
    install: false,
)

metatomic_dlopen_dep = declare_dependency(
    link_with: [metatomic_dlopen],
    dependencies: [dl_dep],
    include_directories: _incdirs + [mta_inc],
)

# Fat linked pot + portable engine plugin only when torch/metatomic are available
if has_metatomic
    # Linked pot (fast) — optional scientific path
    metatomicpot = library(
        'metatomicpot',
        'MetatomicPot.cc',
        dependencies: _deps,
        cpp_args: _args + ['-DRGPOT_METATOMIC_LINKED=1'],
        include_directories: _incdirs + [mta_inc],
        install: not meson.is_subproject(),
    )

    metatomicpot_dep = declare_dependency(
        link_with: [metatomicpot],
        include_directories: _incdirs + [mta_inc],
        compile_args: ['-DRGPOT_METATOMIC_LINKED=1'],
    )

    # Engine plugin for dlopen: torch/metatomic only — do NOT link LJ/CuH2/_linkto
    # (portable plugin; consumers: pip rgpot, eOn RGPOT host).
    _engine_rpath = []
    if is_variable('_torch_libdir')
        _engine_rpath += [_torch_libdir]
    endif
    if is_variable('_py_purelib')
        _engine_rpath += [
            _py_purelib / 'metatensor' / 'lib',
            _py_purelib / 'metatensor_torch' / ('torch-' + _torch_major) / 'lib',
            _py_purelib / 'metatensor' / 'torch' / ('torch-' + _torch_major) / 'lib',
            _py_purelib / 'metatomic' / 'torch' / ('torch-' + _torch_major) / 'lib',
            _py_purelib / 'vesin' / 'lib',
        ]
    endif
    _engine_rpath_args = []
    foreach d : _engine_rpath
        _engine_rpath_args += ['-Wl,-rpath,' + d]
    endforeach
    # Also $ORIGIN for same-dir / mesonpy.libs layout after install
    _engine_rpath_args += ['-Wl,-rpath,$ORIGIN', '-Wl,-rpath,$ORIGIN/../.rgpot.mesonpy.libs']

    metatomic_engine = library(
        'metatomic_engine',
        [
            'MetatomicPot.cc',
            'MetatomicEngineAbi.cc',
        ],
        dependencies: _deps,
        cpp_args: _args + ['-DRGPOT_MTA_ENGINE_BUILD'],
        # No link_with: _linkto — engine must not NEEDED host pot libs
        include_directories: _incdirs + [mta_inc],
        link_args: _engine_rpath_args,
        install: not meson.is_subproject(),
        install_rpath: '$ORIGIN:$ORIGIN/../.rgpot.mesonpy.libs',
        gnu_symbol_visibility: 'default',
    )

    metatomic_engine_dep = declare_dependency(
        link_with: [metatomic_engine],
        include_directories: _incdirs + [mta_inc],
    )
endif
