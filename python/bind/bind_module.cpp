// MIT License
// Copyright 2023--present rgpot developers
//
// Python surface: nanobind (stable ABI / abi3 when Python >= 3.12).
// LJ + MetatomicDlopen frontend (engine .so optional via C ABI).

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>

#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/MetatomicPot/MetatomicConfig.hpp"
#include "rgpot/MetatomicPot/MetatomicDlopen.hpp"
#include "rgpot/types/AtomMatrix.hpp"

namespace nb = nanobind;
using rgpot::types::AtomMatrix;

using NpF64 = nb::ndarray<nb::numpy, double, nb::c_contig, nb::device::cpu>;
using NpI32 = nb::ndarray<nb::numpy, int32_t, nb::c_contig, nb::device::cpu>;

namespace {

AtomMatrix numpy_to_atom_matrix(const NpF64 &arr) {
  if (arr.ndim() != 2 || arr.shape(1) != 3) {
    throw std::invalid_argument("positions must have shape (n_atoms, 3)");
  }
  const auto n = static_cast<size_t>(arr.shape(0));
  AtomMatrix mat(n, 3);
  std::memcpy(mat.data(), arr.data(), n * 3 * sizeof(double));
  return mat;
}

std::vector<int> numpy_to_types(const NpI32 &arr, size_t n_atoms) {
  if (arr.ndim() != 1 || static_cast<size_t>(arr.shape(0)) != n_atoms) {
    throw std::invalid_argument("atom_types length must match n_atoms");
  }
  std::vector<int> types(n_atoms);
  for (size_t i = 0; i < n_atoms; ++i) {
    types[i] = static_cast<int>(arr.data()[i]);
  }
  return types;
}

std::array<std::array<double, 3>, 3> numpy_to_box(const NpF64 &arr) {
  if (arr.ndim() != 2 || arr.shape(0) != 3 || arr.shape(1) != 3) {
    throw std::invalid_argument("box must have shape (3, 3)");
  }
  std::array<std::array<double, 3>, 3> box{};
  const double *p = arr.data();
  for (size_t i = 0; i < 3; ++i)
    for (size_t j = 0; j < 3; ++j)
      box[i][j] = p[i * 3 + j];
  return box;
}

nb::ndarray<nb::numpy, double, nb::c_contig>
atom_matrix_to_numpy(const AtomMatrix &mat) {
  const size_t rows = mat.rows();
  const size_t cols = mat.cols();
  double *buf = new double[rows * cols];
  std::memcpy(buf, mat.data(), rows * cols * sizeof(double));
  nb::capsule owner(buf, [](void *p) noexcept {
    delete[] static_cast<double *>(p);
  });
  return nb::ndarray<nb::numpy, double, nb::c_contig>(buf, {rows, cols}, owner);
}

void flat_box(const std::array<std::array<double, 3>, 3> &box, double out[9]) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      out[i * 3 + j] = box[i][j];
}

nb::tuple evaluate_lj(const NpF64 &positions, const NpI32 &atom_types,
                      const NpF64 &box) {
  if (positions.ndim() != 2 || atom_types.ndim() != 1 || box.ndim() != 2) {
    throw std::invalid_argument(
        "evaluate_lj expects positions (n,3), atom_types (n,), box (3,3)");
  }
  auto pos = numpy_to_atom_matrix(positions);
  auto types = numpy_to_types(atom_types, pos.rows());
  auto cell = numpy_to_box(box);
  rgpot::LJPot pot;
  auto [energy, forces, variance] = pot(pos, types, cell);
  return nb::make_tuple(energy, atom_matrix_to_numpy(forces), variance);
}

nb::tuple evaluate_metatomic_dlopen(const NpF64 &positions,
                                    const NpI32 &atom_types, const NpF64 &box,
                                    const std::string &model_path,
                                    const std::string &engine_path,
                                    const std::string &device) {
  if (positions.ndim() != 2 || atom_types.ndim() != 1 || box.ndim() != 2) {
    throw std::invalid_argument(
        "evaluate_metatomic_dlopen expects positions (n,3), atom_types (n,), "
        "box (3,3)");
  }
  if (model_path.empty()) {
    throw std::invalid_argument("model_path is required");
  }
  auto pos = numpy_to_atom_matrix(positions);
  auto types = numpy_to_types(atom_types, pos.rows());
  auto cell = numpy_to_box(box);

  rgpot::MetatomicConfig cfg;
  cfg.model_path = model_path;
  cfg.engine_path = engine_path;
  cfg.device = device.empty() ? "cpu" : device;

  rgpot::MetatomicDlopen front(cfg);
  if (!front.available()) {
    throw std::runtime_error("MetatomicDlopen: engine not available");
  }

  const size_t n = pos.rows();
  AtomMatrix forces = AtomMatrix::Zero(n, 3);
  double flat[9];
  flat_box(cell, flat);
  rgpot::ForceInput in{n, pos.data(), types.data(), flat};
  rgpot::ForceOut out{forces.data(), 0.0, 0.0};
  // Torch autograd must not run while the Python GIL is held once
  // libtorch_python is loaded (nanobind keeps the GIL by default).
  {
    nb::gil_scoped_release release;
    front.forceImpl(in, &out);
  }
  return nb::make_tuple(out.energy, atom_matrix_to_numpy(forces), out.variance);
}

} // namespace

NB_MODULE(_core, m) {
  m.doc() =
      "rgpot Python bindings (nanobind): LJ core + Metatomic dlopen frontend. "
      "Stable ABI (abi3) when built with Python >= 3.12.";
  m.attr("__version__") = "2.3.1";
  m.attr("has_metatomic_dlopen") = true;

  m.def("evaluate_lj", &evaluate_lj, nb::arg("positions"),
        nb::arg("atom_types"), nb::arg("box"));

  m.def("evaluate_metatomic_dlopen", &evaluate_metatomic_dlopen,
        nb::arg("positions"), nb::arg("atom_types"), nb::arg("box"),
        nb::arg("model_path"), nb::arg("engine_path") = "",
        nb::arg("device") = "cpu",
        "Evaluate forces via MetatomicDlopen (dlopen libmetatomic_engine.so). "
        "engine_path may be empty to use RGPOT_METATOMIC_ENGINE / "
        "package-bundled multi-ABI engine under rgpot/lib/torch-X.Y/.");

  nb::class_<rgpot::LJPot>(m, "LJPot")
      .def(nb::init<>())
      .def(
          "__call__",
          [](rgpot::LJPot &self, const NpF64 &positions,
             const NpI32 &atom_types, const NpF64 &box) {
            auto pos = numpy_to_atom_matrix(positions);
            auto types = numpy_to_types(atom_types, pos.rows());
            auto cell = numpy_to_box(box);
            auto [energy, forces, variance] = self(pos, types, cell);
            return nb::make_tuple(energy, atom_matrix_to_numpy(forces),
                                  variance);
          },
          nb::arg("positions"), nb::arg("atom_types"), nb::arg("box"));
}
