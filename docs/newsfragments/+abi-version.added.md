`rgpot_version()`, `rgpot_version_major()`, `rgpot_version_minor()` and
`rgpot_version_patch()` report the version of the loaded library, so a C caller
can check it against the `RGPOT_VERSION*` macros of the header it compiled
with. `rgpot.h` carried 3.3.0 through the 3.4.0 release; `potctl release sync`
now stamps its macros and `potctl release assert` fails when they drift.
