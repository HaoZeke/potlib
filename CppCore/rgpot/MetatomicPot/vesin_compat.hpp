#pragma once
// MIT License
// Copyright 2023--present rgpot developers
//
// Source-level compatibility for vesin 0.3.x (enum VesinDevice, no
// Options.algorithm) and vesin 0.5+ (struct VesinDevice {type, device_id},
// Options.algorithm). Detection is pure C++ type traits — no parent-build
// macros (RGPOT_VESIN_*) required.

#include <type_traits>
#include <utility>

#include "vesin.h"

namespace rgpot {
namespace vesin_compat {

// True when VesinDevice is the 0.5+ struct with type + device_id members.
template <typename Device, typename = void>
struct is_device_struct : std::false_type {};

template <typename Device>
struct is_device_struct<
    Device, std::void_t<decltype(std::declval<Device>().type),
                        decltype(std::declval<Device>().device_id)>>
    : std::true_type {};

// CPU device handle for the installed vesin headers.
// enum VesinDevice  -> VesinCPU
// struct VesinDevice -> {VesinCPU, 0}
template <typename Device = VesinDevice>
constexpr Device make_cpu_device() {
  if constexpr (is_device_struct<Device>::value) {
    return Device{VesinCPU, /*device_id=*/0};
  } else {
    return static_cast<Device>(VesinCPU);
  }
}

// True when VesinOptions has an algorithm member (vesin 0.5+).
template <typename Options, typename = void>
struct has_algorithm_member : std::false_type {};

template <typename Options>
struct has_algorithm_member<
    Options, std::void_t<decltype(std::declval<Options>().algorithm)>>
    : std::true_type {};

// When algorithm exists, assign its zero/default value without naming
// VesinAutoAlgorithm (absent from older headers). No-op otherwise.
template <typename Options>
void set_algorithm_default(Options &options) {
  if constexpr (has_algorithm_member<Options>::value) {
    options.algorithm = {};
  }
}

} // namespace vesin_compat
} // namespace rgpot
