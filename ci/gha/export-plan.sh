#!/usr/bin/env bash
# Export runtime plan JSON (+ optional full workflow YAML audit bundle).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
OUT_DIR="${1:-ci/gha/out}"
mkdir -p "$OUT_DIR"

if ! command -v nickel >/dev/null 2>&1; then
  echo "error: nickel not on PATH (use: pixi r -e cigen export-plan)" >&2
  exit 1
fi

echo "nickel export plan.ncl -> $OUT_DIR/ci-plan.json"
nickel export --format json ci/gha/plan.ncl -o "$OUT_DIR/ci-plan.json"

# Audit bundle: nickel modules (committed workflows + orchestrator source mirrors).
if [[ "${EXPORT_WORKFLOWS_AUDIT:-1}" == "1" ]]; then
  AUDIT="$OUT_DIR/workflows-audit"
  mkdir -p "$AUDIT"
  while IFS='|' read -r src dst_name; do
    [[ -z "$src" || "$src" =~ ^# ]] && continue
    echo "  audit: $src -> $AUDIT/$dst_name"
    nickel export --format yaml "ci/gha/$src" -o "$AUDIT/$dst_name"
  done <<'SPECS'
# committed (also via gen-gha)
release.ncl|release.yml
release_prepare.ncl|release-prepare.yml
ci_docs.ncl|ci_docs.yml
cosmo_potctl.ncl|cosmo-potctl.yml
ci_doc_commenter.ncl|ci_doc_commenter.yml
# orchestrator job mirrors (audit only; live executor is ci-orchestrator.yml)
build.ncl|build-mirror.yml
potentials.ncl|potentials-mirror.yml
prek.ncl|prek-mirror.yml
docs_quality.ncl|docs_quality-mirror.yml
towncrier.ncl|towncrier-mirror.yml
SPECS
fi

echo "OK: plan at $OUT_DIR/ci-plan.json"
