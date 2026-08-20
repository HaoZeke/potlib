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
  3d9ee78e9926df085d90b2d395c51b7bfc359133 \
  9be1973bd8ddd48eb92e8d1abc047d55bceace44; do
  printf '%s\n' "$section" | grep -Fq "$revision"
  grep -Fq "$revision" "$LOCK"
done

if grep -Fq '?branch=' "$LOCK"; then
  echo "pixi.lock still contains a branch selector" >&2
  exit 1
fi

echo "ok: eonviz companion revisions are immutable and lockfile-aligned"
