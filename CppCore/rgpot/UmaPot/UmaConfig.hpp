#pragma once
// MIT License — config POD for the fairchem UMA / OMol helper frontend

#include <string>

namespace rgpot {

struct UmaConfig {
  /** HuggingFace / fairchem checkpoint name, e.g. uma-s-1p1. */
  std::string model = "uma-s-1p1";
  /** UMA task head. Baker-Chan is molecular: omol. */
  std::string task_name = "omol";
  std::string device = "cpu";
  /** Optional explicit path to uma_helper.py. */
  std::string helper_path;
  /** Optional Python interpreter. Empty uses python3 / RGPOT_UMA_PYTHON. */
  std::string python;
  int charge = 0;
  /** Spin multiplicity (1 = singlet), matches fairchem atoms.info["spin"]. */
  int spin = 1;
};

} // namespace rgpot
