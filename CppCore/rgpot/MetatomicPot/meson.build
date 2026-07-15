# Metatomic: linked pot (fast) + optional engine .so (slow dlopen path)
mta_inc = include_directories('.')

dl_dep = dependency('dl', required: false)
if not dl_dep.found()
    dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)
endif

# Thin frontend — no torch; always part of metatomic feature
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

# Linked pot (fast) — current scientific implementation
metatomicpot = library(
    'metatomicpot',
    'MetatomicPot.cc',
    dependencies: _deps,
    cpp_args: _args + ['-DRGPOT_METATOMIC_LINKED=1'],
    link_with: _linkto,
    include_directories: _incdirs + [mta_inc],
    install: not meson.is_subproject(),
)

metatomicpot_dep = declare_dependency(
    link_with: [metatomicpot],
    include_directories: _incdirs + [mta_inc],
    compile_args: ['-DRGPOT_METATOMIC_LINKED=1'],
)

# Optional engine shared library for dlopen consumers (eOn RGPOT thin host)
metatomic_engine = library(
    'metatomic_engine',
    [
        'MetatomicPot.cc',
        'MetatomicEngineAbi.cc',
    ],
    dependencies: _deps,
    cpp_args: _args + ['-DRGPOT_MTA_ENGINE_BUILD'],
    link_with: _linkto,
    include_directories: _incdirs + [mta_inc],
    install: not meson.is_subproject(),
    gnu_symbol_visibility: 'default',
)

metatomic_engine_dep = declare_dependency(
    link_with: [metatomic_engine],
    include_directories: _incdirs + [mta_inc],
)
