Lockstep monorepo release via cocogitto 7 + towncrier + ``potctl`` (``potctl/``,
not published): ``cog bump`` runs ``potctl release sync`` then towncrier then
``potctl release assert --require-changelog``; ``release.yml`` publishes
``rgpot-core`` on stable ``v*`` tags. CI ``potentials.yml`` builds xtb/tblite and
metatomic/vesin backends.
