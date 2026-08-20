#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$ROOT/pixi.toml"
LOCK="$ROOT/pixi.lock"

section=$(awk '
  /^\[feature\.eonviz\.pypi-dependencies\]$/ { inside = 1; next }
  /^\[/ { inside = 0 }
  inside { print }
' "$MANIFEST")

if printf '%s\n' "$section" | grep -Eq '^[[:space:]]*(branch|tag)[[:space:]]*='; then
  echo "eonviz dependencies must use immutable revisions" >&2
  exit 1
fi

for revision in \
  f71d79b9b9f913c717e4d97dd3ec9fd496f2c208 \
  a652605a61991d2fea86a9f63bfabfc926f6f03c; do
  printf '%s\n' "$section" | grep -Fq "$revision"
  grep -Fq "$revision" "$LOCK"
done

if grep -Fq '?branch=' "$LOCK"; then
  echo "pixi.lock still contains a branch selector" >&2
  exit 1
fi

echo "ok: eonviz companion revisions are immutable and lockfile-aligned"
