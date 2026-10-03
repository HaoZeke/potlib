`LJConfig`, `LJClusterConfig` and `MorseConfig` take `switch_width`: a positive
width replaces the shifted truncation with the C^2 quintic switch
`S(t) = 1 - 10t^3 + 15t^4 - 6t^5` over the last `switch_width` Angstrom below
the cutoff, so the energy, force and curvature reach zero there. The default 0
keeps the shifted truncation and its cache keys.
`scripts/validation/pair_kernels.py` (sympy) checks every pair kernel's force
against `-dE/dx` and the switch's C^2 conditions;
`scripts/validation/quintic_switch.sollya` bounds its binary64 evaluation error.
