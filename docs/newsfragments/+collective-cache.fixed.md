The result cache no longer deadlocks a bound MPI world. A potential that
reports the new `PotCaps::worldCollective` (`CPMDPot` does) has its cache
hit or miss decided jointly over `MPI_COMM_WORLD` through the new
`calculatorsAgree`, so a geometry one rank holds and another misses is
computed on every rank instead of leaving the missing rank alone in the
engine's collective.
