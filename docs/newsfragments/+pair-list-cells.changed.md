LJPot, LJClusterPot, MorsePot and ZBLPot search pairs with linked cells once
the box holds 64 or more cells of one cutoff plus skin, so a fresh search
costs O(n) instead of O(n^2), and fully periodic boxes past 20000 atoms keep
their cached pair list. The cached force loop runs over a sorted CSR half
list with the periodic image recorded at build when cutoff + skin is below
half the box, and keeps each atom's force in registers across its row.
`PairListCache::accumulate` takes a radial kernel returning a `PairTerm`
(energy and `-V'(r)/r`) and returns the energy.
