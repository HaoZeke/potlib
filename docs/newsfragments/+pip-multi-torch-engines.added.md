Metatomic engines are built for each supported torch major
(``rgpot/lib/torch-X.Y/libmetatomic_engine.so``) and selected at runtime from
the installed torch version — same multi-ABI model as metatomic-torch itself.
The pip product covers **torch 2.7 and newer**; earlier majors are out of scope.
