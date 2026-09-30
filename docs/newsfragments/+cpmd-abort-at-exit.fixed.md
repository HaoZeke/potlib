A failed CPMD engine call sets an abort flag, and the exit handler calls `MPI_Abort` instead of a collective `MPI_Finalize` that peers stuck in a broadcast would never reach.
