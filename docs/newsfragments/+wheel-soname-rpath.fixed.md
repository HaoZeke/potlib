Python 3.x wheels import without torch: fat MetatomicPot is no longer
`link_whole`'d into `librgpot` when `with_python` (engine stays dlopen-only),
and the repair step ships `librgpot.so.3` (mesonpy zip dropped the soname link)
plus a RUNPATH into site-packages torch/metatensor/vesin.
