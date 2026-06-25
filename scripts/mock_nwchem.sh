#!/usr/bin/env bash
# Minimal nwchem stand-in for pipeline smoke tests (water STO-3G-like numbers).
# Writes <stem>.out beside the input .nw with energy + gradient lines the CLI parser expects.
set -euo pipefail
inp="${1:-}"
if [[ -z "$inp" || ! -f "$inp" ]]; then
  echo "usage: mock_nwchem.sh input.nw" >&2
  exit 2
fi
stem="${inp%.nw}"
out="${stem}.out"
# Reference-ish H2O RHF/STO-3G total energy (Hartree) + small analytic-like grads
cat >"$out" <<'EOF'
                          NWChem Input Module
     Total SCF energy =      -74.963123456789

                         SCF ENERGY GRADIENTS

    atom               coordinates                        gradient
                 x          y          z           x          y          z
   1 O       0.000000   0.000000   0.222600   0.00000000  0.00000000  0.01234567
   2 H       0.000000   1.427600  -0.890400   0.00000000  0.00876543 -0.00617284
   3 H       0.000000  -1.427600  -0.890400   0.00000000 -0.00876543 -0.00617284
EOF
echo "mock_nwchem: wrote $out" >&2
exit 0
