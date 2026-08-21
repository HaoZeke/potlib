#pragma once
// MIT License — config POD for the linked UMA / OMol metatomic frontend

#include "rgpot/MetatomicPot/MetatomicConfig.hpp"

#include <string>

namespace rgpot {

/**
 * UMA / OMol calculator configuration.
 *
 * The checkpoint is a metatomic TorchScript model (same load path as
 * MetatomicPot). Charge and spin ride as System extra data for the omol
 * head. task_name is recorded on the config and hashed into paramsKey.
 */
struct UmaConfig {
  std::string model_path;
  std::string task_name = "omol";
  std::string device;
  std::string length_unit = "angstrom";
  std::string extensions_directory;
  int charge = 0;
  int spin = 1;
  TorchDeterminismPolicy torch_determinism = TorchDeterminismPolicy::Fast;
  std::string engine_path;

  [[nodiscard]] MetatomicConfig to_metatomic() const {
    MetatomicConfig m;
    m.model_path = model_path;
    m.device = device;
    m.length_unit = length_unit;
    m.extensions_directory = extensions_directory;
    m.torch_determinism = torch_determinism;
    m.engine_path = engine_path;
    m.task_name = task_name;
    m.charge = charge;
    m.spin = spin;
    m.attach_system_extras = true;
    return m;
  }
};

} // namespace rgpot
