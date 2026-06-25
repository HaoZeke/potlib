# NWChemPot: frontend always builds (dlopen libnwchem_engine at runtime).
# Stub static lib always built. libnwchem_engine uses CLI subprocess backend
# (nwchem on PATH). Optional Fortran embed when has_nwchem + has_fortran + root.

nwchem_inc = include_directories('.')

# --- Stub ABI (always; no NWChem required) ---
nwchem_abi_stub = static_library(
    'nwchem_abi_stub',
    'nwchem_c_abi_stub.c',
    include_directories: nwchem_inc,
    install: false,
)

# --- Engine shared lib: CLI backend by default (works with nwchem binary) ---
nwchem_engine_targets = []
nwchem_engine_srcs = ['nwchem_c_abi_cli.c']
nwchem_engine_link_args = []
nwchem_engine_c_args = []
nwchem_engine_f_args = []

# Optional in-process Fortran embed when a full NWChem tree is configured.
if has_nwchem and has_fortran and nwchem_root != ''
    nwchem_engine_srcs = ['nwchem_c_abi.c', 'nwchem_embed.F']
    nwchem_engine_c_args += [
        '-DRGPOT_HAS_NWCHEM=1',
        '-I' + nwchem_root + '/src/include',
        '-I' + nwchem_root + '/src/tools/install/include',
    ]
    nwchem_engine_f_args += ['-DRGPOT_HAS_NWCHEM=1']
    nwchem_engine_link_args += [
        '-L' + nwchem_root + '/lib/' + nwchem_target,
        '-lnwcutil',
    ]
    if host_machine.system() == 'linux'
        nwchem_engine_link_args += ['-Wl,--unresolved-symbols=ignore-all']
    elif host_machine.system() == 'darwin'
        nwchem_engine_link_args += ['-Wl,-undefined,dynamic_lookup']
    endif
endif

nwchem_engine = shared_library(
    'nwchem_engine',
    nwchem_engine_srcs,
    include_directories: _incdirs + [nwchem_inc],
    c_args: nwchem_engine_c_args,
    fortran_args: nwchem_engine_f_args,
    link_args: nwchem_engine_link_args,
    install: not meson.is_subproject(),
)
nwchem_engine_targets += [nwchem_engine]

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
