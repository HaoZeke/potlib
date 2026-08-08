Wheel publish falls back to the `pypi-rgpot` / `testpypi-rgpot`
`PYPI_TOKEN` until a matching trusted publisher is registered.

cibuildwheel `build` must be a space-separated string so cp310, cp311,
and the cp312 abi3 wheel all ship (a TOML array only built cp310).
