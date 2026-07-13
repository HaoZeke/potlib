#pragma once
// MIT License
// Copyright 2023--present rgpot developers
//
// Requires vesin >= 0.5 (VesinDevice is a struct {type, device_id};
// VesinOptions includes algorithm/sorted). Pin via pixi feature.metatomic
// (vesin>=0.5.2,<0.6).

#include <mutex>
#include <string>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"

#include <torch/script.h>

#include "metatensor/torch.hpp"
#include "metatensor/torch/module.hpp"
#include "metatomic/torch.hpp"

#pragma GCC diagnostic pop

#include "rgpot/Potential.hpp"

namespace rgpot {

// Process-global LibTorch execution policy for Metatomic/PET evaluation.
//
// These flags live on at::globalContext() and therefore affect every Torch
// user in the process, not only the MetatomicPot instance that applied them.
// Fast is the default and never mutates global state (fused CUDA attention
// and other nondeterministic kernels remain available). Strict requests
// deterministic algorithms without warn-only fallback and pins scaled-dot-
// product attention to the math SDP backend only (flash, memory-efficient,
// and cuDNN SDP disabled). Callers that need run-to-run force bit-stability
// must opt into Strict explicitly.
enum class TorchDeterminismPolicy {
  Fast = 0,
  Strict = 1,
};

// Apply a TorchDeterminismPolicy to the process-global LibTorch context.
// Strict mutates at::globalContext(); Fast is a no-op that leaves existing
// global flags unchanged (it does not restore a previous Strict setting).
void apply_torch_determinism_policy(TorchDeterminismPolicy policy);

struct MetatomicConfig {
  std::string model_path;
  std::string device;
  std::string length_unit = "angstrom";
  std::string extensions_directory;
  bool check_consistency = false;
  // If > 0, request per-atom energy_uncertainty (when the model exposes it)
  // and write the mean into ForceOut::variance; also log atoms above threshold.
  double uncertainty_threshold = -1.0;
  std::string dtype_override;
  // eOn #287 / #292: multi-orientation handling for models that are not
  // exactly rotationally invariant.
  // n_symmetry_rotations snaps to a rotation GROUP orbit: >=24 -> chiral
  // octahedral (24), >=12 -> tetrahedral (12). Group averaging makes the
  // averaged energy exactly G-invariant and keeps F_avg = -grad E_avg as an
  // exact finite-sum identity (residual SO(3) non-invariance starts at l=4
  // for O, l=3 kept for T). 1 < n < 12 falls back to that many seeded
  // Haar-random orientations (Monte Carlo; 1/sqrt(N) damping only).
  // random_rotation alone is a single rotated evaluation.
  bool random_rotation = false;
  long n_symmetry_rotations = 0;
  // Probe-scatter mode: output E/F come from the UNROTATED evaluation only
  // (one coherent surface steers geometry); n_symmetry_rotations extra
  // orientations are evaluated solely to measure the force-RMS orientation
  // scatter written to ForceOut::variance (an uncertainty certificate,
  // never a geometry signal).
  bool so3_probe_scatter = false;
  // LibTorch determinism policy applied once at construction (process-global).
  // Default Fast preserves throughput; set Strict for explicit reproducibility.
  TorchDeterminismPolicy torch_determinism = TorchDeterminismPolicy::Fast;
};

class MetatomicPot : public Potential<MetatomicPot> {
public:
  explicit MetatomicPot(const MetatomicConfig &config);
  ~MetatomicPot() = default;

  MetatomicPot(const MetatomicPot &) = delete;
  MetatomicPot &operator=(const MetatomicPot &) = delete;

  void forceImpl(const ForceInput &in, ForceOut *out) const override;

private:
  MetatomicConfig m_config;

  mutable metatensor_torch::Module m_model;
  metatomic_torch::ModelCapabilities m_capabilities;
  std::vector<metatomic_torch::NeighborListOptions> m_nl_requests;
  metatomic_torch::ModelEvaluationOptions m_eval_options;

  torch::ScalarType m_dtype;
  torch::Device m_device;
  bool m_check_consistency;
  std::string m_energy_key;
  std::string m_energy_uncertainty_key;
  double m_uncertainty_threshold = -1.0;

  mutable std::mutex m_mutex;
  mutable torch::Tensor m_cached_types; //!< Cached atomic types tensor.
  mutable size_t m_cached_natoms = 0;   //!< Atom count for cached types.

  metatensor_torch::TensorBlock
  computeNeighbors(metatomic_torch::NeighborListOptions request, long nAtoms,
                   const double *positions, const double *box,
                   const bool periodic[3]) const;
};

} // namespace rgpot
