#pragma once
// MIT License
// Copyright 2023--present rgpot developers

#include <cstddef>

namespace rgpot {

// One independent calculator. A NEB image, a dimer end, or any other
// concurrent evaluation is one group. Ranks in a group share a
// subcommunicator. A second band is a second world, not a second
// split of the same one.
struct CalculatorGroup {
  int index = 0;
  int ranks = 1;
  int rank_in_group = 0;
  int world_size = 1;
};

// A backend that must own the subcommunicator (CPMD, and the same
// shape for any other MPI engine) registers this. It is called on
// every rank from bindCalculators. Return the group index, or -1.
using CalculatorHook = int (*)(int ranks_per_calculator);

void addCalculatorHook(CalculatorHook hook);

// Collective on MPI_COMM_WORLD when the process was started under MPI.
// ranks_per_calculator <= 0 means one group for the whole world.
// Returns index -1 when the world cannot be divided that way.
CalculatorGroup bindCalculators(int ranks_per_calculator);

const CalculatorGroup &thisCalculator();

// Copies the group's MPI_Comm into *comm_out when this build has MPI
// and a split exists. comm_bytes is sizeof(MPI_Comm) on the caller.
// Returns 0 when there is no communicator to give.
int calculatorComm(void *comm_out, std::size_t comm_bytes);

// 1 when this build links MPI and bindCalculators can split and share.
int calculatorsUseMpi();

// Ranks in MPI_COMM_WORLD after bindCalculators, 1 before.
int calculatorWorldSize();

// Number of calculators the world is split into. 1 before a split and
// when the world could not be divided.
int calculatorCount();

// Broadcasts bytes from the first rank of calculator `owner` to every
// rank of MPI_COMM_WORLD, so all ranks hold that calculator's result.
// Collective on MPI_COMM_WORLD. Returns 0 without MPI or before a
// split, 1 once the bytes are in place.
int shareFromCalculator(int owner, void *data, std::size_t bytes);

// Registers, once per process, an exit handler that calls MPI_Finalize
// when MPI was initialized and is not yet finalized. An engine that
// initializes MPI inside a host program without finalizing it (CPMD)
// otherwise leaves each rank to exit on its own, and mpirun kills the
// ranks still working as an abnormal termination. MPI_Finalize is
// collective, so ranks that finish first wait for the rest. No-op
// without MPI.
void finalizeMpiAtExit();

} // namespace rgpot
