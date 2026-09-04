#!/usr/bin/env bash
# Install conda-forge s-dftd3 + dftd4 into PREFIX for the cibuildwheel build.
# pkg-config names: s-dftd3, dftd4. Do not pip-install PyPI dftd3 stubs.
set -euo pipefail
PREFIX="${1:-/opt/rgpot-dftd}"
os="$(uname -s)"
arch="$(uname -m)"
plat=""
if [ "$os" = Linux ] && [ "$arch" = x86_64 ]; then
  plat=linux-64
elif [ "$os" = Linux ] && [ "$arch" = aarch64 ]; then
  plat=linux-aarch64
elif [ "$os" = Darwin ] && [ "$arch" = arm64 ]; then
  plat=osx-arm64
elif [ "$os" = Darwin ] && [ "$arch" = x86_64 ]; then
  plat=osx-64
fi
if [ -z "$plat" ]; then
  echo "error: no micromamba tarball for $os $arch" >&2
  exit 1
fi

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
curl -fsSL "https://micro.mamba.pm/api/micromamba/${plat}/latest" \
  | tar -xjv -C "$workdir" bin/micromamba
"$workdir/bin/micromamba" create -y -p "$PREFIX" -c conda-forge \
  "simple-dftd3>=1.2.0,<2" \
  "dftd4>=3.7.0,<5" \
  pkg-config
# so meson/pkg-config see the prefix without relying on shell PATH expansion
mkdir -p "$PREFIX/lib/pkgconfig" "$PREFIX/lib64/pkgconfig"
echo "rgpot-dftd prefix $PREFIX"
"$PREFIX/bin/pkg-config" --modversion s-dftd3
"$PREFIX/bin/pkg-config" --modversion dftd4
