project(
    'rgpot',
    'cpp',
    version: '1.2.0',
    default_options: ['warning_level=2', 'cpp_std=c++20'],
)
# IMPORTANT!! warning_level=3 passes -fimplicit-none
# Many of the older Fortran codes need implicit typing

host_system = host_machine.system()

# Add C++ compiler options
_cppc_early = meson.get_compiler('cpp')
if _cppc_early.get_id() == 'msvc' or _cppc_early.get_id() == 'clang-cl'
    cpp_args = ['/W3']
else
    cpp_args = ['-Wall', '-Wextra', '-Wpedantic']
endif
_args = [cpp_args]  # Extra arguments
_deps = []  # Dependencies
_linkto = []  # All the sub-libraries (potentials)
_incdirs = []  # All the includes
_rgpot_srcs = []

# Add conditionals
if host_system == 'darwin'
    _args += ['-DOSX=TRUE']
    cpp_args += ['-faligned-allocation']
endif

# Languages
add_languages('c', required: true)
# Fortran/CuH2 on all hosts when available. On Darwin/CI, pair with system
# clang++ (not conda arm64-apple-darwin20 clang) so C++ targets link the host
# SDK; fortcuh2 is static on Darwin (packagefiles) and scoped to the cuh2
# target only so LJ/ptlrpc are not polluted with gfortran link flags.
if not get_option('with_rpc_client_only')
    has_fortran = add_languages('fortran', required: false)
else
    has_fortran = false
endif

rpc_enabled = get_option('with_rpc') or get_option('with_rpc_client_only')

if get_option('with_rpc_client_only')
    _args += ['-DRGPOT_RPC_CLIENT_ONLY=TRUE']
endif

cc = meson.get_compiler('c')
cppc = meson.get_compiler('cpp')

# Dependencies
# libm for Unix systems
m_dep = cppc.find_library('m', required: false)
_deps += m_dep
# libstdc++ only on Linux/GCC; Darwin/clang uses libc++ via the driver.
if host_system == 'linux'
    _deps += [declare_dependency(link_args: '-lstdc++')]
endif

# --------------------- Optional dependencies
# Each dependency is controlled by a single flag; examples select
# themselves based on which deps are available (see CppCore/meson.build).

has_eigen = false
if get_option('with_eigen') or (
    get_option('with_examples')
    and not get_option(
    'with_rpc_client_only',
    )
)
    eigen_dep = dependency('eigen3', required: false)
    if not eigen_dep.found()
        eigen_dep = dependency(
            'eigen3',
            subproject: subproject('eigen'),
            required: get_option('with_eigen'),
        )
    endif
    if eigen_dep.found()
        _deps += eigen_dep
        has_eigen = true
    endif
endif

has_xtensor = false
if get_option('with_xtensor')
    _deps += dependency('xtensor')
    _deps += dependency('xtensor-blas')
    _args += ['-DWITH_XTENSOR=TRUE']
    has_xtensor = true
endif

has_fmt = false
if not get_option('pure_lib') or (
    get_option('with_examples')
    and not get_option(
    'with_rpc_client_only',
    )
)
    _args += ['-DNOT_PURE_LIB=TRUE']
    fmt_dep = dependency('fmt', required: true)
    _deps += fmt_dep
    has_fmt = true
endif

has_xtb = false
if get_option('with_xtb')
    xtb_dep = dependency('xtb', required: true)
    _deps += xtb_dep
    _args += ['-DRGPOT_HAS_XTB=TRUE']
    has_xtb = true
endif

# NWChemPot: frontend always builds (dlopen libnwchem_engine).
# Engine shared lib always built with CLI backend (nwchem on PATH).
# has_nwchem=true only when with_nwchem + nwchem_root for optional Fortran embed.
nwchem_root = get_option('nwchem_root')
nwchem_target = get_option('nwchem_target')
has_nwchem = false
if get_option('with_nwchem') and nwchem_root != ''
    _args += ['-DRGPOT_HAS_NWCHEM=TRUE']
    has_nwchem = true
endif
_args += ['-DRGPOT_HAS_NWCHEM_FRONTEND=TRUE']

has_tblite = false
if get_option('with_tblite')
    tblite_dep = dependency('tblite', required: true)
    _deps += tblite_dep
    _args += ['-DRGPOT_HAS_TBLITE=TRUE']
    has_tblite = true
endif

has_metatomic = false
if get_option('with_metatomic')
    # PyTorch C++ API (libtorch) -- found via cmake.
    # Meson's cmake trace often fails to resolve the full linker flags for
    # libtorch_cpu.so. We supplement with an explicit rpath + link_args.
    torch_dep = dependency(
        'Torch',
        method: 'cmake',
        modules: ['torch'],
        required: true,
    )
    _deps += torch_dep

    # PyTorch package paths are discovered without importing torch. Importing
    # torch loads compiled NumPy extensions during configure, so the probe must
    # only use Python's standard library and the installed package metadata.
    _py_probe = run_command(
        find_program('python3'),
        '-c',
        '''
import re
import sysconfig
from pathlib import Path

purelib = Path(sysconfig.get_paths()["purelib"])
version_file = purelib / "torch" / "version.py"
match = re.search(
    r"^__version__\s*=\s*['\"]([^'\"]+)",
    version_file.read_text(),
    re.MULTILINE,
)
if match is None:
    raise SystemExit(f"torch version not found in {version_file}")
version = ".".join(match.group(1).split("+")[0].split(".")[:2])
print(purelib)
print(version)
''',
        check: true,
    ).stdout().strip().split('\n')
    _py_purelib = _py_probe[0]
    _torch_major = _py_probe[1]
    _torch_libdir = _py_purelib / 'torch' / 'lib'
    _deps += declare_dependency(
        link_args: [
            '-L' + _torch_libdir,
            '-Wl,-rpath,' + _torch_libdir,
            '-ltorch_cpu',
        ],
    )

    # metatensor-torch / metatomic-torch headers live in the
    # python interpreter's site-packages under
    # <purelib>/metatensor/torch/torch-X.Y/include and
    # <purelib>/metatomic/torch/torch-X.Y/include. The cmake /
    # pkg-config dependency lookups don't see those, and
    # find_library() doesn't add include paths. Ask sysconfig for
    # the right purelib and propagate -isystem includes manually.
    _mts_inc_dirs = [
        _py_purelib / 'metatensor' / 'include',
        _py_purelib / 'metatensor' / 'torch' / ('torch-' + _torch_major) / 'include',
        _py_purelib / 'metatomic' / 'torch' / ('torch-' + _torch_major) / 'include',
        _py_purelib / 'vesin' / 'include',
    ]
    _mts_inc_args = []
    foreach d : _mts_inc_dirs
        _mts_inc_args += ['-isystem', d]
    endforeach

    # metatensor-torch: try pkg-config, then cmake, then find_library
    mts_dep = dependency('metatensor_torch', required: false)
    if not mts_dep.found()
        mts_dep = dependency('metatensor-torch', required: false)
    endif
    if not mts_dep.found()
        mts_dep = dependency(
            'metatensor_torch',
            method: 'cmake',
            required: false,
        )
    endif
    if not mts_dep.found()
        mts_dep = declare_dependency(
            dependencies: [
                cppc.find_library(
                    'metatensor',
                    dirs: [_py_purelib / 'metatensor' / 'lib'],
                ),
                cppc.find_library(
                    'metatensor_torch',
                    dirs: [
                        _py_purelib / 'metatensor' / 'torch' / (
                            'torch-' + _torch_major
                        ) / 'lib',
                    ],
                ),
            ],
            compile_args: _mts_inc_args,
        )
    endif
    _deps += mts_dep

    # metatomic-torch: try pkg-config, then cmake, then find_library
    mta_dep = dependency('metatomic_torch', required: false)
    if not mta_dep.found()
        mta_dep = dependency('metatomic-torch', required: false)
    endif
    if not mta_dep.found()
        mta_dep = cppc.find_library(
            'metatomic_torch',
            dirs: [
                _py_purelib / 'metatomic' / 'torch' / ('torch-' + _torch_major) / 'lib',
            ],
        )
    endif
    _deps += mta_dep

    # vesin neighbor list library
    _vesin_libdir = _py_purelib / 'vesin' / 'lib'
    vesin_dep = dependency('vesin', required: false)
    if not vesin_dep.found()
        vesin_dep = cppc.find_library(
            'vesin',
            dirs: [_vesin_libdir],
        )
    endif
    _deps += vesin_dep
    _deps += declare_dependency(
        link_args: ['-Wl,-rpath,' + _vesin_libdir],
    )

    _args += ['-DRGPOT_HAS_METATOMIC=TRUE']
    has_metatomic = true
endif

# --------------------- Subprojects
# fortcuh2 is only needed by the CuH2 potential — do NOT add it to global
# `_deps`. Doing so pulls -lgfortran / gcc lib paths into every intermediate
# dylib (LJ, ptlrpc, …) and breaks conda macOS clang++ links (missing
# __Unwind_Resume / libSystem symbols).
fortcuh2_dep = disabler()
if has_fortran
    # Darwin: packagefiles/fortcuh2 builds a static lib (see fortcuh2.wrap
    # patch_directory) so the final C++ target resolves system symbols.
    fortcuh2_proj = subproject('fortcuh2')
    fortcuh2_dep = fortcuh2_proj.get_variable('fortcuh2_dep')
    _args += ['-DRGPOT_HAS_FORTRAN=TRUE']
endif

# --------------------- Rust Core (rgpot-core)
#
# Build the Rust crate via Cargo and link the resulting static library.
# The crate also generates include/rgpot.h via cbindgen.

if get_option('with_rust_core')
    cargo = find_program('cargo', required: true)

    # Determine Cargo build profile and library name
    if get_option('buildtype') == 'release'
        _cargo_profile_dir = 'release'
        _cargo_args = ['build', '--release']
    else
        _cargo_profile_dir = 'debug'
        _cargo_args = ['build']
    endif

    # Add feature flags for the Rust crate
    _cargo_features = []
    if rpc_enabled
        _cargo_features += ['rpc']
        _args += ['-DRGPOT_HAS_RPC=TRUE']
    endif
    if get_option('with_cache')
        _cargo_features += ['cache']
    endif

    if _cargo_features.length() > 0
        _cargo_args += ['--features', ','.join(_cargo_features)]
    endif

    _rgpot_core_dir = meson.current_source_dir() / 'rgpot-core'

    # Build the Rust crate; produces librgpot_core.a and include/rgpot.h
    rgpot_rust_target = custom_target(
        'rgpot_rust_core',
        command: [
            cargo,
            _cargo_args,
            '--manifest-path',
            _rgpot_core_dir / 'Cargo.toml',
            '--target-dir',
            meson.current_build_dir() / 'cargo-target',
        ],
        output: ['librgpot_core.a'],
        build_by_default: true,
        console: true,
    )

    # Link the Rust static library
    _rust_target_dir = meson.current_build_dir() / 'cargo-target' / _cargo_profile_dir
    rgpot_rust_lib = cppc.find_library(
        'rgpot_core',
        dirs: [_rust_target_dir],
        required: true,
        static: true,
    )

    # Thread and dl libraries needed by the Rust runtime
    thread_dep = dependency('threads')
    dl_dep = cppc.find_library('dl', required: false)

    _deps += [rgpot_rust_lib, thread_dep, dl_dep]

    # Include paths: generated C header + C++ RAII wrappers
    _incdirs += [
        include_directories('rgpot-core' / 'include'),  # rgpot.h (cbindgen)
        include_directories('include'),  # rgpot/*.hpp (C++ RAII)
    ]
endif

# --------------------- Projects
_incdirs += [include_directories('CppCore')]
subdir('CppCore')
rgpot_dep = declare_dependency(
    include_directories: _incdirs,
    link_with: _linkto,
    compile_args: _args,
    dependencies: _deps,
)
