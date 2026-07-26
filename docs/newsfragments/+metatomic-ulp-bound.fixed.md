The strict-determinism metatomic test no longer requires two matched force
calls to agree bit for bit, because the provider does not offer that.
Instrumenting the path shows rgpot handing the model identical inputs --
positions, cell, and neighbour list all hash the same on every SO3 pass of
both calls -- and getting energies back that differ by one to two ulp on a
call chosen at random. It reproduces with `OMP_NUM_THREADS=1`, with the
TorchScript profiling executor frozen, and with the pair vectors copied
into torch-owned storage, so it is neither thread-count reassociation, nor
graph specialization, nor alignment of the buffers rgpot passes in. The
test now bounds the energy at four ulp and the force components at an
absolute floor scaled to the largest component, which still fails on any
drift larger than the provider's own noise.
