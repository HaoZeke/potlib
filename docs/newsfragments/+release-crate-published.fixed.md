A release tag no longer fails when `publish.yml` has already uploaded `rgpot-core` to crates.io: `release.yml` skips `cargo publish` for a version that exists, so its GitHub release job still runs.
