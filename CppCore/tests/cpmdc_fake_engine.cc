#include "rgpot/CPMDPot/cpmd_c_abi.h"
#include "rgpot/rpc/Potentials.capnp.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/array.h>
#include <kj/exception.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct CPMDCSession {
  std::vector<::capnp::word> params;
};

namespace {

bool has_flat_message(const void *msg, size_t msg_size_bytes) {
  return msg != nullptr && msg_size_bytes >= sizeof(::capnp::word) &&
         (msg_size_bytes % sizeof(::capnp::word)) == 0;
}

::ForceInput::Reader read_force_input(const void *force_input_capnp,
                                      size_t force_input_capnp_size_bytes,
                                      ::capnp::FlatArrayMessageReader &reader) {
  (void)force_input_capnp;
  (void)force_input_capnp_size_bytes;
  return reader.getRoot<::ForceInput>();
}

bool force_input_ok(const ::ForceInput::Reader &input, size_t *force_count,
                    double *cell_zz) {
  const auto positions = input.getPos();
  const auto atomic_numbers = input.getAtmnrs();
  const auto box = input.getBox();
  if (positions.size() == 0 || positions.size() % 3 != 0)
    return false;
  const size_t n_atoms = positions.size() / 3;
  if (atomic_numbers.size() != n_atoms || box.size() != 9)
    return false;
  if (std::string(input.getLengthUnit().cStr()) != "angstrom" ||
      std::string(input.getEnergyUnit().cStr()) != "eV")
    return false;
  *force_count = positions.size();
  *cell_zz = box[8];
  return std::isfinite(*cell_zz) && *cell_zz > 0.0;
}

std::vector<::capnp::word> make_result(size_t force_count, double cell_zz) {
  ::capnp::MallocMessageBuilder msg;
  auto result = msg.initRoot<::PotentialResult>();
  result.setEnergy(0.75 + 0.001 * cell_zz);
  auto forces = result.initForces(static_cast<unsigned int>(force_count));
  for (unsigned int i = 0; i < forces.size(); ++i)
    forces.set(i, 0.011 + 0.001 * static_cast<double>(i));
  auto words = ::capnp::messageToFlatArray(msg);
  std::vector<::capnp::word> out(words.size());
  std::memcpy(out.data(), words.begin(), words.asBytes().size());
  return out;
}

CPMDCResult fail_result(const char *message) {
  CPMDCResult r;
  r.ok = 0;
  r.energy_h = 0.0;
  std::snprintf(r.message, sizeof(r.message), "%s", message);
  return r;
}

CPMDCResult ok_result(const char *message) {
  CPMDCResult r;
  r.ok = 1;
  r.energy_h = 0.0;
  std::snprintf(r.message, sizeof(r.message), "%s", message);
  return r;
}

} // namespace

extern "C" {

int cpmdc_set_params(const void *params_capnp, size_t params_capnp_size_bytes) {
  return has_flat_message(params_capnp, params_capnp_size_bytes) ? 0 : -1;
}

CPMDCResult cpmdc_energy_gradient(
    int n_atoms, const double *positions_ang, const int *atomic_numbers,
    const void *params_capnp, size_t params_capnp_size_bytes,
    double *grad_h_bohr) {
  (void)n_atoms;
  (void)positions_ang;
  (void)atomic_numbers;
  (void)params_capnp;
  (void)params_capnp_size_bytes;
  (void)grad_h_bohr;
  return fail_result("fake engine requires session result path");
}

CPMDCSession *cpmdc_session_create(const void *params_capnp,
                                   size_t params_capnp_size_bytes) {
  if (!has_flat_message(params_capnp, params_capnp_size_bytes))
    return nullptr;
  auto *session = new CPMDCSession;
  const auto *words = static_cast<const ::capnp::word *>(params_capnp);
  session->params.assign(words,
                         words + params_capnp_size_bytes / sizeof(*words));
  return session;
}

void cpmdc_session_destroy(CPMDCSession *session) { delete session; }

size_t cpmdc_potential_result_size_for_force_input(
    const void *force_input_capnp, size_t force_input_capnp_size_bytes) {
  if (!has_flat_message(force_input_capnp, force_input_capnp_size_bytes))
    return 0;
  try {
    auto words = kj::arrayPtr(static_cast<const ::capnp::word *>(force_input_capnp),
                              force_input_capnp_size_bytes /
                                  sizeof(::capnp::word));
    ::capnp::FlatArrayMessageReader reader(words);
    size_t force_count = 0;
    double cell_zz = 0.0;
    if (!force_input_ok(read_force_input(force_input_capnp,
                                         force_input_capnp_size_bytes, reader),
                        &force_count, &cell_zz))
      return 0;
    return make_result(force_count, cell_zz).size() * sizeof(::capnp::word);
  } catch (const kj::Exception &) {
    return 0;
  }
}

CPMDCResult cpmdc_session_calculate_result(
    CPMDCSession *session, const void *force_input_capnp,
    size_t force_input_capnp_size_bytes, void *potential_result_capnp,
    size_t potential_result_capnp_capacity_bytes,
    size_t *potential_result_capnp_size_bytes) {
  if (session == nullptr ||
      !has_flat_message(force_input_capnp, force_input_capnp_size_bytes) ||
      potential_result_capnp_size_bytes == nullptr)
    return fail_result("invalid fake session arguments");
  try {
    auto words = kj::arrayPtr(static_cast<const ::capnp::word *>(force_input_capnp),
                              force_input_capnp_size_bytes /
                                  sizeof(::capnp::word));
    ::capnp::FlatArrayMessageReader reader(words);
    size_t force_count = 0;
    double cell_zz = 0.0;
    if (!force_input_ok(read_force_input(force_input_capnp,
                                         force_input_capnp_size_bytes, reader),
                        &force_count, &cell_zz))
      return fail_result("invalid fake ForceInput");
    const auto result_words = make_result(force_count, cell_zz);
    const size_t required = result_words.size() * sizeof(::capnp::word);
    *potential_result_capnp_size_bytes = required;
    if (potential_result_capnp == nullptr ||
        potential_result_capnp_capacity_bytes < required)
      return fail_result("result buffer too small");
    std::memcpy(potential_result_capnp, result_words.data(), required);
    return ok_result("session result ok");
  } catch (const kj::Exception &ex) {
    return fail_result(ex.getDescription().cStr());
  }
}

const char *cpmdc_version(void) { return "cpmdc-fake/0.2"; }

int cpmdc_available(void) { return 1; }

} // extern "C"
