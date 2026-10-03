The result cache no longer deadlocks a bound MPI world. A potential that
reports the new `PotCaps::groupCollective` (`CPMDPot` does) has its cache
hit or miss decided jointly over its calculator through the new
`calculatorAgree`, so a geometry one rank holds and another misses is
computed on every rank of the calculator instead of leaving the missing
rank alone in the engine's collective.
