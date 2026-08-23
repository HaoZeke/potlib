// MIT License — UmaPot: vesin neighbor list + AOTInductor .pt2

#include "rgpot/UmaPot/UmaPot.hpp"

#include "rgpot/MetatomicPot/vesin_compat.hpp"
#include "vesin.h"

#include <array>
#include <vector>

#include <torch/csrc/inductor/aoti_package/model_package_loader.h>
#include <torch/torch.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rgpot {
namespace {



bool ends_with(const std::string &s, const std::string &suf) {
  return s.size() >= suf.size() &&
         s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

torch::Device parse_device(const std::string &name) {
  if (name.empty() || name == "cpu")
    return torch::kCPU;
  if (name.rfind("cuda", 0) == 0)
    return torch::Device(name);
  return torch::kCPU;
}

struct EdgeList {
  std::vector<int64_t> neighbor;
  std::vector<int64_t> center;
  std::vector<float> offsets; // nedges * 3
};

EdgeList vesin_fairchem_edges(const ForceInput &in, double cutoff,
                              int max_neighbors) {
  VesinOptions options{};
  vesin_compat::fill_neighbor_options(options, cutoff, /*full_list=*/true);
  options.return_distances = true;

  VesinNeighborList nl{};
  VesinDevice cpu = vesin_compat::make_cpu_device();
  const char *err = nullptr;
  bool pbc[3] = {true, true, true};
  const int status = vesin_neighbors(
      reinterpret_cast<const double(*)[3]>(in.pos), in.nAtoms,
      reinterpret_cast<const double(*)[3]>(in.box), pbc, cpu, options, &nl,
      &err);
  if (status != EXIT_SUCCESS) {
    std::string msg = "UmaPot: vesin_neighbors failed";
    if (err)
      msg += ": " + std::string(err);
    vesin_free(&nl);
    throw std::runtime_error(msg);
  }

  struct Cand {
    int64_t n{0};
    int64_t c{0};
    float s0{0};
    float s1{0};
    float s2{0};
    double dist{0};
  };
  std::vector<Cand> keep;
  keep.reserve(nl.length);
  const double *box = in.box;
  for (size_t k = 0; k < nl.length; ++k) {
    const int64_t i = static_cast<int64_t>(nl.pairs[k][0]);
    const int64_t j = static_cast<int64_t>(nl.pairs[k][1]);
    double dist = 0.0;
    if (nl.distances != nullptr) {
      dist = nl.distances[k];
    } else {
      const double sx = static_cast<double>(nl.shifts[k][0]);
      const double sy = static_cast<double>(nl.shifts[k][1]);
      const double sz = static_cast<double>(nl.shifts[k][2]);
      const double dx = in.pos[3 * j] - in.pos[3 * i] + sx * box[0] +
                        sy * box[3] + sz * box[6];
      const double dy = in.pos[3 * j + 1] - in.pos[3 * i + 1] + sx * box[1] +
                        sy * box[4] + sz * box[7];
      const double dz = in.pos[3 * j + 2] - in.pos[3 * i + 2] + sx * box[2] +
                        sy * box[5] + sz * box[8];
      dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    if (dist < 1e-8)
      continue;
    Cand c;
    c.n = j;
    c.c = i;
    c.s0 = static_cast<float>(nl.shifts[k][0]);
    c.s1 = static_cast<float>(nl.shifts[k][1]);
    c.s2 = static_cast<float>(nl.shifts[k][2]);
    c.dist = dist;
    keep.push_back(c);
  }
  vesin_free(&nl);

  if (max_neighbors > 0) {
    std::vector<std::vector<size_t>> by_center(in.nAtoms);
    for (size_t k = 0; k < keep.size(); ++k)
      by_center[static_cast<size_t>(keep[k].c)].push_back(k);
    std::vector<Cand> trimmed;
    trimmed.reserve(keep.size());
    for (auto &idxs : by_center) {
      std::sort(idxs.begin(), idxs.end(), [&](size_t a, size_t b) {
        return keep[a].dist < keep[b].dist;
      });
      if (static_cast<int>(idxs.size()) > max_neighbors)
        idxs.resize(static_cast<size_t>(max_neighbors));
      for (size_t id : idxs)
        trimmed.push_back(keep[id]);
    }
    keep.swap(trimmed);
  }

  EdgeList out;
  out.neighbor.reserve(keep.size());
  out.center.reserve(keep.size());
  out.offsets.reserve(keep.size() * 3);
  for (const auto &c : keep) {
    out.neighbor.push_back(c.n);
    out.center.push_back(c.c);
    out.offsets.push_back(c.s0);
    out.offsets.push_back(c.s1);
    out.offsets.push_back(c.s2);
  }
  return out;
}

} // namespace

struct UmaPot::Impl {
  torch::Device device{torch::kCPU};
  torch::ScalarType dtype{torch::kFloat32};
  double cutoff{6.0};
  double molecular_box{0.0};
  int max_neighbors{300};
  std::unique_ptr<torch::inductor::AOTIModelPackageLoader> loader;
  mutable std::mutex mutex;
};

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

void UmaPot::ensureLoaded() const {
  if (m_impl->loader)
    return;
  c10::InferenceMode guard;
  m_impl->loader =
      std::make_unique<torch::inductor::AOTIModelPackageLoader>(
          m_config.model_path);
  // The package's embedded metadata is the runtime contract: one
  // producer (scripts/export_uma_aoti.py), the .pt2 self-describing.
  const auto meta = m_impl->loader->get_metadata();
  auto num = [&](const char *key, double fallback) {
    const auto it = meta.find(key);
    if (it == meta.end() || it->second.empty())
      return fallback;
    try {
      return std::stod(it->second);
    } catch (...) {
      return fallback;
    }
  };
  m_impl->cutoff = num("cutoff", m_impl->cutoff);
  m_impl->molecular_box = num("molecular_box", m_impl->molecular_box);
  m_impl->max_neighbors = static_cast<int>(
      num("max_neighbors", static_cast<double>(m_impl->max_neighbors)));
  const auto dt = meta.find("pos_dtype");
  if (dt != meta.end() && dt->second == "float64")
    m_impl->dtype = torch::kFloat64;
}

UmaPot::UmaPot(const UmaConfig &config)
    : Potential(PotType::Uma), m_config(config) {
  if (m_config.model_path.empty()) {
    throw std::runtime_error(
        "UmaPot: model_path is required (AOTI package .pt2)");
  }
  if (!ends_with(m_config.model_path, ".pt2")) {
    throw std::runtime_error(
        "UmaPot: model_path must be an AOTI .pt2 from "
        "scripts/export_uma_aoti.py (not a metatomic/torchscript .pt)");
  }
  recomputeParamsKey();
  m_impl = std::make_unique<Impl>();
  m_impl->device = parse_device(m_config.device);
  m_impl->max_neighbors = m_config.max_neighbors;
  m_impl->cutoff = m_config.cutoff;
  if (!(m_impl->cutoff > 0.0) || !std::isfinite(m_impl->cutoff))
    m_impl->cutoff = 6.0;
}

UmaPot::~UmaPot() = default;

void UmaPot::setChargeSpin(int charge, int spin) {
  m_config.charge = charge;
  m_config.spin = spin;
  recomputeParamsKey();
}

void UmaPot::forceImpl(const ForceInput &in, ForceOut *out) const {
  if (in.nAtoms == 0)
    throw std::runtime_error("UmaPot: nAtoms == 0");

  // Under the molecular-box convention the caller's cell is replaced by
  // the sidecar's cube and positions re-center into it. Energies and
  // forces are translation invariant, so only the graph changes.
  const bool molecular = m_impl->molecular_box > 0.0;
  std::vector<double> mol_pos;
  std::array<double, 9> mol_box{};
  if (molecular) {
    const double L = m_impl->molecular_box;
    mol_pos.assign(in.pos, in.pos + 3 * in.nAtoms);
    double c[3] = {0.0, 0.0, 0.0};
    for (size_t i = 0; i < in.nAtoms; ++i)
      for (int d = 0; d < 3; ++d)
        c[d] += mol_pos[3 * i + d];
    for (int d = 0; d < 3; ++d)
      c[d] /= static_cast<double>(in.nAtoms);
    for (size_t i = 0; i < in.nAtoms; ++i)
      for (int d = 0; d < 3; ++d)
        mol_pos[3 * i + d] += 0.5 * L - c[d];
    mol_box = {L, 0.0, 0.0, 0.0, L, 0.0, 0.0, 0.0, L};
  }
  const ForceInput local{in.nAtoms, molecular ? mol_pos.data() : in.pos,
                         in.atmnrs, molecular ? mol_box.data() : in.box};

  const EdgeList edges =
      vesin_fairchem_edges(local, m_impl->cutoff, m_impl->max_neighbors);
  const auto n = static_cast<int64_t>(local.nAtoms);
  const auto nedges = static_cast<int64_t>(edges.neighbor.size());
  if (nedges == 0)
    throw std::runtime_error("UmaPot: vesin produced no edges");

  auto opts_f = torch::TensorOptions()
                    .dtype(m_impl->dtype)
                    .device(torch::kCPU);
  auto opts_l = torch::TensorOptions().dtype(torch::kLong).device(torch::kCPU);

  auto pos = torch::empty({n, 3}, torch::dtype(torch::kFloat64));
  auto pos_a = pos.accessor<double, 2>();
  for (int64_t i = 0; i < n; ++i) {
    pos_a[i][0] = local.pos[3 * i];
    pos_a[i][1] = local.pos[3 * i + 1];
    pos_a[i][2] = local.pos[3 * i + 2];
  }
  pos = pos.to(opts_f);

  auto atomic_numbers = torch::empty({n}, opts_l);
  auto z_a = atomic_numbers.accessor<int64_t, 1>();
  for (int64_t i = 0; i < n; ++i)
    z_a[i] = local.atmnrs[i];

  auto cell = torch::empty({1, 3, 3}, torch::dtype(torch::kFloat64));
  auto cell_a = cell.accessor<double, 3>();
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      cell_a[0][r][c] = local.box[3 * r + c];
  cell = cell.to(opts_f);

  auto pbc = torch::tensor({{true, true, true}},
                           torch::TensorOptions().dtype(torch::kBool));
  auto edge_index = torch::empty({2, nedges}, opts_l);
  auto ei = edge_index.accessor<int64_t, 2>();
  for (int64_t e = 0; e < nedges; ++e) {
    ei[0][e] = edges.neighbor[static_cast<size_t>(e)];
    ei[1][e] = edges.center[static_cast<size_t>(e)];
  }
  auto cell_offsets =
      torch::from_blob(const_cast<float *>(edges.offsets.data()),
                       {nedges, 3}, torch::dtype(torch::kFloat32))
          .clone()
          .to(opts_f);
  auto charge = torch::tensor({static_cast<int64_t>(m_config.charge)}, opts_l);
  auto spin = torch::tensor({static_cast<int64_t>(m_config.spin)}, opts_l);
  auto batch = torch::zeros({n}, opts_l);
  auto natoms = torch::tensor({n}, opts_l);

  if (m_impl->device != torch::kCPU) {
    pos = pos.to(m_impl->device);
    atomic_numbers = atomic_numbers.to(m_impl->device);
    cell = cell.to(m_impl->device);
    pbc = pbc.to(m_impl->device);
    edge_index = edge_index.to(m_impl->device);
    cell_offsets = cell_offsets.to(m_impl->device);
    charge = charge.to(m_impl->device);
    spin = spin.to(m_impl->device);
    batch = batch.to(m_impl->device);
    natoms = natoms.to(m_impl->device);
  }

  std::vector<torch::Tensor> inputs = {pos,         atomic_numbers, cell,
                                       pbc,         edge_index,     cell_offsets,
                                       charge,      spin,           batch,
                                       natoms};

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  ensureLoaded();
  c10::InferenceMode guard;
  auto outputs = m_impl->loader->run(inputs);
  if (outputs.size() < 2)
    throw std::runtime_error("UmaPot: AOTI package returned <2 tensors");

  const double energy = outputs[0].to(torch::kFloat64).cpu().item<double>();
  auto forces = outputs[1].to(torch::kFloat64).cpu().contiguous();
  if (forces.numel() != n * 3)
    throw std::runtime_error("UmaPot: force tensor shape mismatch");
  const double *fp = forces.data_ptr<double>();
  for (int64_t i = 0; i < n * 3; ++i)
    out->F[i] = fp[i];
  out->energy = energy;
  out->variance = 0.0;
}

} // namespace rgpot
