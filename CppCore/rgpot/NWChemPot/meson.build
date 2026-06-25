# NWChemPot: frontend always builds (dlopen libnwchem_engine at runtime).
# Stub always built. Real engine ONLY when has_nwchem (with_nwchem + nwchem_root
# + Fortran) — in-process embed via nwchem_c_abi.c + nwchem_embed.F. No CLI.

nwchem_inc = include_directories('.')

nwchem_abi_stub = static_library(
    'nwchem_abi_stub',
    'nwchem_c_abi_stub.c',
    include_directories: nwchem_inc,
    install: false,
)

nwchem_engine_targets = []
if has_nwchem
    if not has_fortran
        error('with_nwchem requires a Fortran compiler for nwchem_embed.F')
    endif
    if nwchem_root == ''
        error('with_nwchem requires -Dnwchem_root=<NWCHEM_TOP>')
    endif

    nwchem_engine_c_args = [
        '-DRGPOT_HAS_NWCHEM=1',
        '-I' + nwchem_root + '/src/include',
        '-I' + nwchem_root + '/src/tools/install/include',
    ]
    nwchem_engine_f_args = [
        '-DRGPOT_HAS_NWCHEM=1',
        '-I' + nwchem_root + '/src/include',
        '-I' + nwchem_root + '/src/tools/install/include',
    ]
    nwchem_engine_link_args = [
        '-L' + nwchem_root + '/lib/' + nwchem_target,
        # Minimal stubs; unresolved NWChem symbols resolved when engine is
        # loaded into a process that already linked full NWChem, or via
        # RPATH to a complete lib/ tree. Prefer a fully-built NWCHEM_TOP.
        '-lnwctask',
        '-lnwcutil',
    ]
    if host_machine.system() == 'linux'
        nwchem_engine_link_args += ['-Wl,--unresolved-symbols=ignore-all']
    elif host_machine.system() == 'darwin'
        nwchem_engine_link_args += ['-Wl,-undefined,dynamic_lookup']
    endif

    nwchem_engine = shared_library(
        'nwchem_engine',
        ['nwchem_c_abi.c', 'nwchem_embed.F'],
        include_directories: _incdirs + [nwchem_inc],
        c_args: nwchem_engine_c_args,
        fortran_args: nwchem_engine_f_args,
        link_args: nwchem_engine_link_args,
        install: not meson.is_subproject(),
    )
    nwchem_engine_targets += [nwchem_engine]
endif

nwchem_dl_dep = dependency('dl', required: false)
if not nwchem_dl_dep.found()
    nwchem_dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)
endif

nwchempot = static_library(
    'nwchempot',
    'NWChemPot.cc',
    dependencies: _deps + [nwchem_dl_dep],
    cpp_args: _args + ['-DRGPOT_HAS_NWCHEM_FRONTEND=TRUE'],
    include_directories: _incdirs + [nwchem_inc],
    install: not meson.is_subproject(),
)

nwchempot_dep = declare_dependency(
    link_with: nwchempot,
    dependencies: [nwchem_dl_dep],
    include_directories: _incdirs + [nwchem_inc],
    compile_args: ['-DRGPOT_HAS_NWCHEM_FRONTEND=TRUE'],
)

nwchem_abi_stub_dep = declare_dependency(
    link_with: nwchem_abi_stub,
    include_directories: nwchem_inc,
)
