# NWChemPot: frontend always builds (dlopen libnwchem_engine at runtime).
# Stub static lib always built. Optional libnwchem_engine when has_nwchem.

nwchem_inc = include_directories('.')

# --- Stub ABI (always; no NWChem required) ---
nwchem_abi_stub = static_library(
    'nwchem_abi_stub',
    'nwchem_c_abi_stub.c',
    include_directories: nwchem_inc,
    install: false,
)

# --- Optional real engine (Fortran embed + C ABI) ---
nwchem_engine_targets = []
if has_nwchem
    nwchem_engine_srcs = ['nwchem_c_abi.c']
    nwchem_engine_link_with = []
    nwchem_engine_link_args = []
    nwchem_engine_inc = [nwchem_inc]
    nwchem_engine_deps = []
    nwchem_c_args = ['-DRGPOT_HAS_NWCHEM=1']

    if nwchem_root != ''
        # Pass include/lib paths via compiler/linker flags (avoids missing-dir errors
        # when NWChem layout varies across installs).
        nwchem_c_args += [
            '-I' + nwchem_root + '/src/include',
            '-I' + nwchem_root + '/src/tools/install/include',
        ]
        nwchem_engine_link_args += [
            '-L' + nwchem_root + '/lib/' + nwchem_target,
            '-lnwcutil',
        ]
    endif

    if has_fortran
        nwchem_engine_srcs += ['nwchem_embed.F']
    endif

    if host_machine.system() == 'linux'
        nwchem_engine_link_args += ['-Wl,--unresolved-symbols=ignore-all']
    elif host_machine.system() == 'darwin'
        nwchem_engine_link_args += ['-Wl,-undefined,dynamic_lookup']
    endif

    nwchem_engine = shared_library(
        'nwchem_engine',
        nwchem_engine_srcs,
        include_directories: _incdirs + nwchem_engine_inc,
        c_args: nwchem_c_args,
        fortran_args: ['-DRGPOT_HAS_NWCHEM=1'],
        link_args: nwchem_engine_link_args,
        dependencies: nwchem_engine_deps,
        install: not meson.is_subproject(),
    )
    nwchem_engine_targets += [nwchem_engine]
endif

# --- Frontend (always; dlopen engine) ---
nwchempot = static_library(
    'nwchempot',
    'NWChemPot.cc',
    dependencies: _deps,
    cpp_args: _args + ['-DRGPOT_HAS_NWCHEM_FRONTEND=TRUE'],
    include_directories: _incdirs + [nwchem_inc],
    install: not meson.is_subproject(),
)

nwchempot_dep = declare_dependency(
    link_with: nwchempot,
    include_directories: _incdirs + [nwchem_inc],
    compile_args: ['-DRGPOT_HAS_NWCHEM_FRONTEND=TRUE'],
)

# Export stub for ABI probe test
nwchem_abi_stub_dep = declare_dependency(
    link_with: nwchem_abi_stub,
    include_directories: nwchem_inc,
)
