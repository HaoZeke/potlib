// Link libeindir_core, produced by `cargo capi build` in the eindir checkout.
// rgpot-core consumes eindir THROUGH its C library (the hourglass waist), not
// via a Cargo dependency on eindir-core. EINDIR_LIB_DIR points at eindir's
// cargo-c output (target/<triple>/release); we link the cdylib dynamically.
fn main() {
    let dir = std::env::var("EINDIR_LIB_DIR")
        .unwrap_or_else(|_| "../target/x86_64-unknown-linux-gnu/release".to_string());
    println!("cargo:rustc-link-search=native={dir}");
    println!("cargo:rustc-link-lib=dylib=eindir_core");
    println!("cargo:rerun-if-env-changed=EINDIR_LIB_DIR");
}
