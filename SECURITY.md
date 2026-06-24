# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| Latest `main` / newest `vX.Y.Z` tag | Yes (fixes land on `main` first) |
| Older minor lines | Best-effort only; please upgrade to the latest tag |

rgpot is primarily consumed from **git tags** (C++) and **crates.io `rgpot-core`** (Rust).
Security fixes are released via normal semver bumps (`cog bump` + `v*` tag + `release.yml`).

## Reporting a vulnerability

Please **do not** open a public GitHub issue for unfixed vulnerabilities.

1. Email the maintainers privately (see repository owner / `CODEOWNERS`), **or**
2. Use GitHub **Security Advisories** / private vulnerability reporting if enabled for this repository.

Include: affected versions/tags, reproduction or impact, and whether a fix is already proposed.

We aim to acknowledge reports promptly and coordinate disclosure once a fix is tagged.

## Scope

In scope: remote/code-exec issues in shipped library code, unsafe defaults in public APIs,
dependency issues in directly vendored or required components.

Out of scope (unless trivially fixed): issues only in unreleased experimental branches,
user-supplied models/binaries (e.g. TorchScript weights), or third-party backends (xtb,
tblite, PyTorch/metatomic) without a clear rgpot-specific misuse.
