# Psi4Pot: frontend links only Psi4Pot.cc (dlopen engine at runtime).
# psi4_engine: shared library compiled against Psi4 headers, unresolved symbols
# resolved at runtime from libpsi4 (loaded with RTLD_GLOBAL before the engine).

psi4_stub_inc = include_directories('psi4_stubs')

psi4_inc_opt = get_option('psi4_includedir')
psi4_header_incs = [psi4_stub_inc]
if psi4_inc_opt != ''
    psi4_header_incs += include_directories(psi4_inc_opt)
endif

# Conventional third_party install: pass -Dpsi4_includedir=... from build_psi4_backend.sh
# (avoids meson fs module requirement at configure time).

psi4_engine_link_args = []
if host_machine.system() == 'linux'
    psi4_engine_link_args += ['-Wl,--unresolved-symbols=ignore-all']
elif host_machine.system() == 'darwin'
    psi4_engine_link_args += ['-Wl,-undefined,dynamic_lookup']
endif

psi4_engine = shared_library(
    'psi4_engine',
    'psi4_engine.cc',
    include_directories: _incdirs + psi4_header_incs,
    cpp_args: _args + ['-DRGPOT_PSI4_ENGINE=1'],
    link_args: psi4_engine_link_args,
    install: not meson.is_subproject(),
    # No link dependency on libpsi4; symbols resolved via dlopen(RTLD_GLOBAL).
)

psi4pot = static_library(
    'psi4pot',
    'Psi4Pot.cc',
    dependencies: _deps,
    cpp_args: _args + ['-DRGPOT_HAS_PSI4=TRUE'],
    include_directories: _incdirs,
    install: not meson.is_subproject(),
)

# Export for parent meson.build (rgpotentials += [psi4pot])
psi4pot_dep = declare_dependency(
    link_with: psi4pot,
    include_directories: _incdirs,
    compile_args: ['-DRGPOT_HAS_PSI4=TRUE'],
)
