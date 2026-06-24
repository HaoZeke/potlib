//! CI helpers: repo preflight, macOS/pixi host-SDK env (replaces ad-hoc bash in GHA).

use std::env;
use std::path::{Path, PathBuf};

use crate::lockstep;

pub type Result<T> = std::result::Result<T, String>;

/// Paths that must exist for a healthy rgpot checkout (build/release CI).
pub fn required_paths(root: &Path) -> Vec<PathBuf> {
    vec![
        root.join("meson.build"),
        root.join("CMakeLists.txt"),
        root.join("pixi.toml"),
        root.join("rgpot-core").join("Cargo.toml"),
        root.join("potctl").join("Cargo.toml"),
        root.join("towncrier.toml"),
        root.join("cog.toml"),
    ]
}

pub fn preflight(root: &Path, lockstep_check: bool) -> Result<()> {
    let mut missing = Vec::new();
    for p in required_paths(root) {
        if !p.is_file() {
            missing.push(p.display().to_string());
        }
    }
    if !missing.is_empty() {
        return Err(format!("missing required files:\n  {}", missing.join("\n  ")));
    }

    if lockstep_check {
        lockstep::assert_lockstep(root, None, false)?;
    } else {
        // Still surface versions for logs without failing on drift (rare).
        let meson = lockstep::read_meson_version(root).unwrap_or_else(|_| "?".into());
        println!("preflight: meson version = {meson} (lockstep not enforced)");
    }

    println!("preflight ok ({})", root.display());
    Ok(())
}

/// Environment variables for macOS + pixi: use host Apple clang/SDK, keep conda
/// dep search paths (LIBRARY_PATH/CPATH/DYLD_FALLBACK), prefer conda gfortran.
///
/// Emits `export KEY=value` / `unset KEY` lines suitable for `eval "$(potctl ci darwin-env)"`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DarwinEnvPlan {
    pub exports: Vec<(String, String)>,
    pub unsets: Vec<String>,
    pub meson_extra: String,
}

pub fn plan_darwin_env(
    os_name: &str,
    conda_prefix: Option<&str>,
    sdkroot: Option<&str>,
) -> Option<DarwinEnvPlan> {
    if os_name != "macOS" && os_name != "Darwin" {
        return None;
    }

    let mut exports: Vec<(String, String)> = Vec::new();
    let unsets: Vec<String> = vec![
        "CONDA_BUILD_SYSROOT".into(),
        "CC_FOR_BUILD".into(),
        "CXX_FOR_BUILD".into(),
        "CPP_FOR_BUILD".into(),
        "CMAKE_ARGS".into(),
        "host_alias".into(),
        "build_alias".into(),
        "CFLAGS".into(),
        "CXXFLAGS".into(),
        "CPPFLAGS".into(),
        "LDFLAGS".into(),
    ];

    if let Some(sdk) = sdkroot.filter(|s| !s.is_empty()) {
        exports.push(("SDKROOT".into(), sdk.to_string()));
    }

    for (k, v) in [
        ("CC", "/usr/bin/clang"),
        ("CXX", "/usr/bin/clang++"),
        ("OBJC", "/usr/bin/clang"),
        ("OBJCXX", "/usr/bin/clang++"),
        ("LD", "/usr/bin/clang++"),
    ] {
        exports.push((k.into(), v.into()));
    }

    if let Some(prefix) = conda_prefix.filter(|s| !s.is_empty()) {
        let p = Path::new(prefix);
        let lib = p.join("lib");
        let inc = p.join("include");
        let pc = p.join("lib").join("pkgconfig");

        exports.push((
            "CMAKE_PREFIX_PATH".into(),
            prepend_path_var(env::var_os("CMAKE_PREFIX_PATH"), prefix),
        ));
        exports.push((
            "PKG_CONFIG_PATH".into(),
            prepend_path_var(
                env::var_os("PKG_CONFIG_PATH"),
                &pc.to_string_lossy(),
            ),
        ));
        exports.push((
            "LIBRARY_PATH".into(),
            prepend_path_var(env::var_os("LIBRARY_PATH"), &lib.to_string_lossy()),
        ));
        exports.push((
            "CPATH".into(),
            prepend_path_var(env::var_os("CPATH"), &inc.to_string_lossy()),
        ));
        exports.push((
            "DYLD_FALLBACK_LIBRARY_PATH".into(),
            prepend_path_var(
                env::var_os("DYLD_FALLBACK_LIBRARY_PATH"),
                &lib.to_string_lossy(),
            ),
        ));

        let gfortran = p.join("bin").join("gfortran");
        if gfortran.is_file() || env::var_os("POTCTL_TEST_GFORTRAN").is_some() {
            // In tests we may force gfortran presence without the file.
            let fc = if gfortran.is_file() {
                gfortran.to_string_lossy().into_owned()
            } else {
                format!("{prefix}/bin/gfortran")
            };
            for k in ["FC", "F77", "F90", "F95", "GFORTRAN"] {
                exports.push((k.into(), fc.clone()));
            }
        }
    }

    Some(DarwinEnvPlan {
        exports,
        unsets,
        meson_extra: "-Ddefault_library=static".into(),
    })
}

fn prepend_path_var(existing: Option<std::ffi::OsString>, first: &str) -> String {
    match existing {
        Some(e) if !e.is_empty() => format!("{first}:{}", e.to_string_lossy()),
        _ => first.to_string(),
    }
}

/// Shell-escape a value for use inside double quotes in `export KEY="..."`.
pub fn shell_double_quote(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for c in s.chars() {
        match c {
            '"' | '\\' | '$' | '`' => {
                out.push('\\');
                out.push(c);
            }
            _ => out.push(c),
        }
    }
    out.push('"');
    out
}

pub fn render_darwin_env_shell(plan: &DarwinEnvPlan) -> String {
    let mut lines = Vec::new();
    lines.push("# potctl ci darwin-env — macOS host clang/SDK + pixi dep paths".to_string());
    for k in &plan.unsets {
        lines.push(format!("unset {k} || true"));
    }
    for (k, v) in &plan.exports {
        lines.push(format!("export {k}={}", shell_double_quote(v)));
    }
    lines.push(format!(
        "export POTCTL_MESON_EXTRA={}",
        shell_double_quote(&plan.meson_extra)
    ));
    lines.push("".to_string());
    lines.join("\n")
}

/// Detect runner OS from env (GITHUB_ACTIONS sets RUNNER_OS; else uname-ish heuristic).
pub fn detect_runner_os() -> String {
    if let Ok(os) = env::var("RUNNER_OS") {
        return os;
    }
    if let Ok(os) = env::var("OSTYPE") {
        if os.contains("darwin") {
            return "macOS".into();
        }
    }
    #[cfg(target_os = "macos")]
    {
        return "macOS".into();
    }
    #[cfg(not(target_os = "macos"))]
    {
        "Linux".into()
    }
}

pub fn resolve_sdkroot_hint() -> Option<String> {
    if let Ok(s) = env::var("SDKROOT") {
        if !s.is_empty() {
            return Some(s);
        }
    }
    // Do not shell out in unit tests; real CI sets SDKROOT via xcrun in workflow
    // before eval, or pass --sdkroot. Optional: try xcrun if present.
    None
}

pub fn print_darwin_env(
    force: bool,
    conda_prefix: Option<&str>,
    sdkroot: Option<&str>,
) -> Result<()> {
    let os = detect_runner_os();
    let prefix = conda_prefix
        .map(str::to_string)
        .or_else(|| env::var("CONDA_PREFIX").ok())
        .or_else(|| env::var("PIXI_PROJECT_ROOT").ok().and_then(|_| env::var("CONDA_PREFIX").ok()));

    let sdk = sdkroot
        .map(str::to_string)
        .or_else(resolve_sdkroot_hint);

    let plan = if force {
        plan_darwin_env("macOS", prefix.as_deref(), sdk.as_deref())
    } else {
        plan_darwin_env(&os, prefix.as_deref(), sdk.as_deref())
    };

    match plan {
        Some(p) => {
            print!("{}", render_darwin_env_shell(&p));
            Ok(())
        }
        None => {
            // Non-macOS: emit a no-op so `eval "$(…)"` is safe on all matrix legs.
            println!("# potctl ci darwin-env: no-op (runner os = {os})");
            println!("export POTCTL_MESON_EXTRA=\"\"");
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;

    #[test]
    fn required_paths_are_under_root() {
        let root = Path::new("/tmp/rgpot-fake");
        let paths = required_paths(root);
        assert!(paths.iter().all(|p| p.starts_with(root)));
        assert!(paths.iter().any(|p| p.ends_with("potctl/Cargo.toml")));
    }

    #[test]
    fn darwin_plan_none_on_linux() {
        assert!(plan_darwin_env("Linux", Some("/conda"), Some("/sdk")).is_none());
    }

    #[test]
    fn darwin_plan_sets_clang_and_unsets_conda_cross() {
        let p = plan_darwin_env("macOS", Some("/opt/conda"), Some("/SDKs/MacOSX.sdk")).unwrap();
        assert!(p.exports.iter().any(|(k, v)| k == "CC" && v == "/usr/bin/clang"));
        assert!(p.exports.iter().any(|(k, v)| k == "SDKROOT" && v.contains("MacOSX")));
        assert!(p.unsets.contains(&"LDFLAGS".to_string()));
        assert!(p.unsets.contains(&"CONDA_BUILD_SYSROOT".to_string()));
        assert_eq!(p.meson_extra, "-Ddefault_library=static");
        assert!(p.exports.iter().any(|(k, v)| k == "LIBRARY_PATH" && v.starts_with("/opt/conda/lib")));
        assert!(p.exports.iter().any(|(k, v)| k == "CPATH" && v.starts_with("/opt/conda/include")));
    }

    #[test]
    fn darwin_plan_gfortran_when_forced_in_tests() {
        env::set_var("POTCTL_TEST_GFORTRAN", "1");
        let p = plan_darwin_env("macOS", Some("/opt/conda"), None).unwrap();
        env::remove_var("POTCTL_TEST_GFORTRAN");
        assert!(p.exports.iter().any(|(k, v)| k == "FC" && v.ends_with("gfortran")));
    }

    #[test]
    fn shell_quote_escapes_meta() {
        assert_eq!(shell_double_quote(r#"a"b"#), r#""a\"b""#);
        assert_eq!(shell_double_quote("x$y"), r#""x\$y""#);
    }

    #[test]
    fn render_includes_exports_and_unsets() {
        let p = plan_darwin_env("macOS", Some("/c"), Some("/sdk")).unwrap();
        let s = render_darwin_env_shell(&p);
        assert!(s.contains("unset LDFLAGS || true"));
        assert!(s.contains("export CC="));
        assert!(s.contains("POTCTL_MESON_EXTRA="));
        assert!(s.contains("/sdk"));
    }

    #[test]
    fn semver_still_ok_via_lockstep_module() {
        assert!(lockstep::semver_ok("1.2.0-rc.1"));
    }
}
