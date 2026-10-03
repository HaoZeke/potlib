`CPMDPot::forceImpl` exchanges force errors on its calculator's communicator
instead of `MPI_COMM_WORLD`. A host that hands calculator groups an uneven
batch (two NEB endpoints on four groups) left the computing groups in that
world exchange while the idle groups waited in `shareFromCalculator`, and
every grouped run hung at its first such batch. A failure still prints on
every rank of the failing calculator and aborts the whole world.
