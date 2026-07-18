Install headers and ``rgpot.pc`` (``nwchempot`` / ``cpmdpot`` / ``ptlrpc``; no
torch or xTB at link time) so eOn and other hosts can prefer
``dependency('rgpot')`` over the Meson subproject wrap. Engines stay runtime
dlopen. See ``docs/eon_pkgconfig.md``.
