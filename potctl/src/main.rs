//! potctl — rgpot project control plane (release lockstep, CI helpers, cosmo APE).

mod ci;
#[cfg(feature = "cosmo-host")]
mod cosmo;
mod lockstep;

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::process::ExitCode;

#[derive(Parser, Debug)]
#[command(
    name = "potctl",
    version,
    about = "rgpot project control plane",
    long_about = "Repo-local CLI for release lockstep, CI helpers, and Cosmopolitan APE build. \
Run from the rgpot checkout (walks up for meson.build + pixi.toml + rgpot-core/)."
)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand, Debug)]
enum Commands {
    /// Release / version lockstep (meson, CMake, cargo, towncrier, pixi workspace).
    Release {
        #[command(subcommand)]
        action: ReleaseCmd,
    },
    /// Continuous integration helpers (preflight, darwin/pixi env, …).
    Ci {
        #[command(subcommand)]
        action: CiCmd,
    },
    /// Cosmopolitan APE (`potctl.com`) build orchestration (Rust only; no project bash).
    /// Host-only (`cosmo-host` feature); not compiled into the APE binary itself.
    #[cfg(feature = "cosmo-host")]
    Cosmo {
        #[command(subcommand)]
        action: CosmoCmd,
    },
}

#[cfg(feature = "cosmo-host")]
#[derive(Subcommand, Debug)]
enum CosmoCmd {
    /// Build potctl as Cosmopolitan APE via nightly rustc + cosmocc linker shim.
    ///
    /// Requires COSMO (cosmo monorepo after `make toolchain`), rustup nightly, and
    /// host `potctl-cosmo-ld` (built automatically if missing). Env: POTCTL_COSMO_OUT,
    /// POTCTL_COSMO_ARCHS, COSMO_CC, COSMOCC_ARCHES (see potctl/cosmo/README.md).
    Build,
}

#[derive(Subcommand, Debug)]
enum ReleaseCmd {
    /// Write the same semver into all lockstep surfaces.
    Sync {
        /// Semver (optional leading v), e.g. 1.2.3 or v1.2.3-rc.1
        version: String,
    },
    /// Assert lockstep surfaces agree; optionally require CHANGELOG section.
    Assert {
        /// Expected semver (optional; often the git tag without v).
        version: Option<String>,
        /// Require CHANGELOG.md to contain ## [version] (or meson version if omitted).
        #[arg(long, env = "REQUIRE_CHANGELOG")]
        require_changelog: bool,
    },
    /// Print or write one CHANGELOG.md section for GH Release body.
    Notes {
        version: String,
        /// Write section to this path instead of stdout.
        #[arg(short = 'o', long = "out")]
        out: Option<PathBuf>,
    },
}

#[derive(Subcommand, Debug)]
enum CiCmd {
    /// Check required files (+ optional lockstep assert). Safe first step in GHA jobs.
    Preflight {
        /// Skip lockstep version agreement (only check files exist).
        #[arg(long)]
        no_lockstep: bool,
    },
    /// Print shell exports for macOS + pixi host SDK/clang fix; no-op on Linux.
    /// Usage in GHA: `eval "$(potctl ci darwin-env)"` then meson/cmake with `$POTCTL_MESON_EXTRA`.
    DarwinEnv {
        /// Emit macOS plan even if RUNNER_OS is not macOS (local testing).
        #[arg(long)]
        force: bool,
        /// Override CONDA_PREFIX (default: env CONDA_PREFIX from pixi shell).
        #[arg(long)]
        conda_prefix: Option<String>,
        /// Override SDKROOT (default: env SDKROOT; set via `xcrun --show-sdk-path` in workflow first).
        #[arg(long)]
        sdkroot: Option<String>,
    },
}

fn run() -> Result<(), String> {
    let cli = Cli::parse();
    let root = lockstep::repo_root()?;

    match cli.command {
        Commands::Release { action } => match action {
            ReleaseCmd::Sync { version } => lockstep::sync_versions(&root, &version),
            ReleaseCmd::Assert {
                version,
                require_changelog,
            } => lockstep::assert_lockstep(&root, version.as_deref(), require_changelog),
            ReleaseCmd::Notes { version, out } => {
                lockstep::extract_changelog(&root, &version, out.as_deref())
            }
        },
        Commands::Ci { action } => match action {
            CiCmd::Preflight { no_lockstep } => ci::preflight(&root, !no_lockstep),
            CiCmd::DarwinEnv {
                force,
                conda_prefix,
                sdkroot,
            } => ci::print_darwin_env(force, conda_prefix.as_deref(), sdkroot.as_deref()),
        },
        #[cfg(feature = "cosmo-host")]
        Commands::Cosmo { action } => match action {
            CosmoCmd::Build => cosmo::build_ape(&root),
        },
    }
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: potctl: {e}");
            ExitCode::FAILURE
        }
    }
}
