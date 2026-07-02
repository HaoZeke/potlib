# Find dependencies and tools
capnp_rpc_dep = dependency('capnp-rpc')
capnp = find_program('capnp')
python = find_program('python3', 'python')

# Canonical contract pinned via subprojects/potentials-schema.wrap; the two
# vendored Potentials.capnp copies must stay byte-identical to it.
potentials_schema_proj = subproject('potentials-schema')
potentials_schema_canonical = potentials_schema_proj.get_variable('potentials_capnp_file')
schema_sync_check = [
  '-c',
  'import filecmp, sys; sys.exit(0 if filecmp.cmp(sys.argv[1], sys.argv[2], shallow=False) else "vendored Potentials.capnp diverges from pinned potentials-schema release")',
]
test(
  'schema-sync-canonical-rpc',
  python,
  args: schema_sync_check + [
    meson.current_source_dir() / 'Potentials.capnp',
    potentials_schema_canonical,
  ],
  suite: 'schema',
)
test(
  'schema-sync-canonical-core',
  python,
  args: schema_sync_check + [
    meson.project_source_root() / 'rgpot-core/schema/Potentials.capnp',
    potentials_schema_canonical,
  ],
  suite: 'schema',
)

gen_rpc = custom_target(
    'gen_rpc',
    input: 'Potentials.capnp',
    output: ['Potentials.capnp.cpp', 'Potentials.capnp.h'],
    command: [
        python,
        meson.current_source_dir() / 'capnp_compile.py',
        capnp,
        '@OUTDIR@',
        meson.current_source_dir(),
        '@INPUT@',
    ],
)
# Use the target object itself for includes to ensure proper dependency tracking
_incdirs += include_directories('.')

ptlrpc = library(
    'ptlrpc',
    gen_rpc,
    dependencies: _deps + capnp_rpc_dep,
    cpp_args: _args,
    include_directories: _incdirs,
    install: not meson.is_subproject(),
)

_ptlrpc_extra_deps = [capnp_rpc_dep]
if host_machine.system() == 'windows'
    _ptlrpc_extra_deps += cppc.find_library('ws2_32')
endif

ptlrpc_dep = declare_dependency(
    link_with: ptlrpc,
    sources: gen_rpc,
    include_directories: _incdirs,
    dependencies: _ptlrpc_extra_deps,
)

pot_bridge = library(
    'pot_client_bridge',
    ['pot_bridge.cc', gen_rpc],
    link_with: ptlrpc,
    dependencies: [capnp_rpc_dep],
    cpp_args: _args,
    # gen_rpc outputs live in the build dir; include it explicitly so
    # Potentials.capnp.h is found even when only pot_bridge.cc is compiled first.
    include_directories: _incdirs + [include_directories('.')],
    install: not meson.is_subproject(),
)

pot_bridge_dep = declare_dependency(
    link_with: pot_bridge,
)
