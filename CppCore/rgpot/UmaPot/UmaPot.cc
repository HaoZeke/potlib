// MIT License — in-process UMA / OMol via MetatomicPot

#include "rgpot/UmaPot/UmaPot.hpp"

#include <stdexcept>

namespace rgpot {

void UmaPot::recomputeParamsKey() {
  Fnv1a fp;
  fp.u64(kKernelVersion);
  fp.str(m_config.model_path);
  fp.str(m_config.task_name);
  fp.str(m_config.device);
  fp.i64(m_config.charge);
  fp.i64(m_config.spin);
  m_paramsKey = fp.h;
}

UmaPot::UmaPot(const UmaConfig &config)
    : Potential(PotType::Uma), m_config(config) {
  if (m_config.model_path.empty()) {
    throw std::runtime_error(
        "UmaPot: model_path is required (metatomic TorchScript .pt)");
  }
  recomputeParamsKey();
  m_inner = std::make_unique<MetatomicPot>(m_config.to_metatomic());
}

void UmaPot::setChargeSpin(int charge, int spin) {
  m_config.charge = charge;
  m_config.spin = spin;
  recomputeParamsKey();
  m_inner->setChargeSpin(charge, spin);
}

void UmaPot::forceImpl(const ForceInput &in, ForceOut *out) const {
  m_inner->forceImpl(in, out);
}

} // namespace rgpot
