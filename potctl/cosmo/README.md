# potctl Cosmopolitan APE (`potctl.com`)

**Live on PR/release CI (gating).** One `potctl.com` (Actually Portable Executable)
built via [Cosmopolitan Libc](https://justine.lol/cosmopolitan/) + community Rust
target (`x86_64-unknown-linux-cosmo` / `aarch64-unknown-linux-cosmo` from
[ahgamut/rust-ape-example](https://github.com/ahgamut/rust-ape-example)).

CI builds once on Linux, then smokes the **same** artifact on Linux and macOS with
`ci preflight` and `release assert` (release-command completeness for the APE path).

Normal `potctl` (musl Linux + fat macOS via `setup-ci-tools` / `ensure-potctl`)
remains the default install for `ci-orchestrator.yml` (build/potentials stages) and `release*.yml`
jobs. Cosmo is an additional **gating** check that the APE binary works for
release-relevant commands on both OSes—not a full replacement of every potctl
consumer in one step.

## What is *not* involved

- **`cargo-c`**: C-ABI libraries; irrelevant to APE.
- Stock `cargo build -p potctl --release` on `gnu`/`darwin` targets (local default).
- True multi-arch fat APE (dual `x86_64`+`aarch64` rustc objects); CI uses `x86_64`
  rustc with aarch64 companion stubs for cosmocc’s multi-arch fat pass only.

## Prerequisites (local)

1. Cosmopolitan monorepo with toolchain (`COSMO` = absolute path to cosmo root).
2. `rustup` **nightly** + `rust-src` component.
3. Host `cargo` + `rustup` (normal build deps Cosmo documents). Cosmo orchestration
   is **Rust only** (`potctl cosmo build` + `potctl-cosmo-ld`); no project-local
   shell scripts.

See [rust-ape-example README](https://github.com/ahgamut/rust-ape-example) for
the full Cosmo `make toolchain` dance; first time is heavy; CI caches `.cosmo-build`.

## Build

From the **rgpot repository root**:

```bash
export COSMO=/path/to/cosmopolitan   # after make -j MODE= toolchain
cargo build --release -p potctl --bins   # host potctl + potctl-cosmo-ld
export PATH="$(pwd)/target/release:$PATH"
potctl cosmo build
# -> potctl/cosmo/out/potctl.com
./potctl/cosmo/out/potctl.com --version
./potctl/cosmo/out/potctl.com ci preflight
./potctl/cosmo/out/potctl.com release assert
```

Env knobs:

| Variable | Default | Meaning |
|----------|---------|---------|
| `COSMO` | required | Cosmopolitan repo root |
| `POTCTL_COSMO_ARCHS` | `x86_64` (add `aarch64` for fat APE rustc) | Space-separated arches to compile |
| `POTCTL_COSMO_OUT` | `potctl/cosmo/out` | Output directory |
| `POTCTL_COSMO_SKIP_APELINK` | unset | If `1`, keep `.com.dbg` only |
| `POTCTL_COSMO_LINKER_TRACE` | `0` | If `1`, print cosmo linker shim trace |

**Libc gaps:** `potctl-cosmo-ld` compiles and links a small `cosmo-libc-compat.o` (currently a
`waitid` stub returning `ENOSYS`) because rustc nightly `std` process/unix/pidfd references
`waitid`, which Cosmopolitan does not provide. Extend that C snippet if future nightlies add
more missing symbols.
| `COSMOCC_ARCHES` | `x86_64` | Passed through to cosmocc (spike default: single-arch APE) |
| `COSMO_CC` | auto | Prefer `cosmocc` (APE CRT/specs); fallback `x86_64-linux-cosmo-gcc` |

## Link recipe (do not regress casually)

`potctl-cosmo-ld` (Rust; target JSON `linker`, built with host `cargo build -p potctl --bin potctl-cosmo-ld`):

1. Prefer **cosmocc** for the final link (supplies APE CRT/specs).
2. Extract `.o` members from `.rlib` archives so rustc rlibs participate in the cosmo link.
3. For cosmocc’s aarch64 fat pass, plant real aarch64 ELF companions under
   `dirname(input)/.aarch64/` (never copy x86_64 objects there).
4. One freestanding aarch64 stub defining **`main`** (first companion only).
5. **Static** pad stubs for all other companions (avoids multidef of pad/`main`).

Orchestration lives in `potctl cosmo build` (`potctl/src/cosmo.rs`); data only under
`potctl/cosmo/` (target JSON + this README).

## CI (gating)

Workflow: `.github/workflows/cosmo-potctl.yml` (`potctl Cosmopolitan APE`)

- **Gating** on PR/push (no `continue-on-error` on build or smoke).
- Caches Cosmopolitan tree (`.cosmo-build`) and cargo cosmo target (`target/cosmo-x86_64`).
- Build job on `ubuntu-latest`: host `cargo build -p potctl --bins` → `potctl cosmo build`
  → `potctl.com` → `ci preflight` + `release assert`.
- Upload artifact `potctl-cosmo-ape`.
- Smoke jobs on `ubuntu-latest` and `macos-latest` download the **same** binary;
  run `--version`/`--help`, `ci preflight`, `release assert` (no cosmo rebuild on macOS).
- Warm-cache steady state is on the order of minutes (not a full uncached `make toolchain`
  every run). Cold cache/first run is intentionally expensive once per cache key.

The old `cosmo-potctl-spike.yml` alias was removed; use `cosmo-potctl.yml` only.
only) so old links/path filters do not 404; do not add jobs there.

## Known limits

- Rust `std` errno/constants are often **Linux-shaped** even inside an APE;
  macOS may hit subtle wrong-errno issues even when the loader starts—smoke must
  stay strict (`ci preflight` / `release assert` exit 0), not weakened to “starts only”.
- Nightly + custom target + Cosmo version skew can break; pin via cache keys / `COSMO_REF`.
- First Cosmo toolchain build is slow; not suitable as the only potctl on every matrix leg.
- Full replacement of musl/fat `setup-ci-tools` across all workflows is optional follow-up
  (`variant: cosmo` install path), not required for this gating check.
