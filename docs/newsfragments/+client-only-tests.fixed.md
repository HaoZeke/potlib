Fix ``-Dwith_rpc_client_only=true -Dwith_tests=true``: use ``pot_bridge_dep``
(was undefined ``rgpot_bridge_dep``), link ``units.cc`` into unit tests when
``rgpot_core`` is not built, and soft-skip bridge stress cases when potserv is
not running (lazy client connect). Catch2 configure/link/test succeed for the
client frontend product without a live server.
