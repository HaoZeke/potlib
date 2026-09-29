A CPMD run inside a host program under mpirun finalizes MPI at exit, so ranks that finish first wait for the others instead of mpirun killing them as an abnormal termination.
