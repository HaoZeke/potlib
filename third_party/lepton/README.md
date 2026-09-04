# Vendored OpenMM Lepton

MIT expression parser from OpenMM (`libraries/lepton`). Analytic
`ParsedExpression::differentiate` plus compile. Not conda-forge
muparser. Not the `leptonica` image library.

Source: https://github.com/openmm/openmm/tree/8.6.0/libraries/lepton

Pin: OpenMM tag `8.6.0` (commit `c6173db6e8edd705eb59172bd21e9ce69c572405`).
See `PIN`.

Layout matches upstream: `include/Lepton.h`, `include/lepton/*.h`, `src/*.cpp`.

Meson compiles these sources only with `-Dwith_expr=true`.
