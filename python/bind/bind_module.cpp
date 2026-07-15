// MIT License
// Copyright 2023--present rgpot developers
//
// Python surface: always-on LJ + MetatomicDlopen frontend (engine .so optional).

#include <array>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "rgpot/LennardJones/LJPot.hpp"
#include "rgpot/MetatomicPot/MetatomicConfig.hpp"
#include "rgpot/MetatomicPot/MetatomicDlopen.hpp"
#include "rgpot/types/AtomMatrix.hpp"

namespace py = pybind11;
using rgpot::types::AtomMatrix;

namespace {

AtomMatrix numpy_to_atom_matrix(const py::array_t<double> &arr) {
  auto buf = arr.unchecked<2>();
  if (buf.shape(1) != 3) {
    throw std::invalid_argument("positions must have shape (n_atoms, 3)");
  }
  const auto n = static_cast<size_t>(buf.shape(0));
  AtomMatrix mat(n, 3);
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < 3; ++j) {
      mat(i, j) = buf(i, j);
    }
  }
  return mat;
}

std::vector<int> numpy_to_types(const py::array_t<int> &arr, size_t n_atoms) {
  auto buf = arr.unchecked<1>();
  if (static_cast<size_t>(buf.shape(0)) != n_atoms) {
    throw std::invalid_argument("atom_types length must match n_atoms");
  }
  std::vector<int> types(n_atoms);
  for (size_t i = 0; i < n_atoms; ++i) {
    types[i] = buf(i);
  }
  return types;
}

std::array<std::array<double, 3>, 3>
numpy_to_box(const py::array_t<double> &arr) {
  auto buf = arr.unchecked<2>();
  if (buf.shape(0) != 3 || buf.shape(1) != 3) {
    throw std::invalid_argument("box must have shape (3, 3)");
  }
  std::array<std::array<double, 3>, 3> box{};
  for (size_t i = 0; i < 3; ++i) {
    for (size_t j = 0; j < 3; ++j) {
      box[i][j] = buf(i, j);
    }
  }
  return box;
}

py::array_t<double> atom_matrix_to_numpy(const AtomMatrix &mat) {
  auto out = py::array_t<double>({mat.rows(), mat.cols()});
  auto buf = out.mutable_unchecked<2>();
  for (size_t i = 0; i < mat.rows(); ++i) {
    for (size_t j = 0; j < mat.cols(); ++j) {
      buf(i, j) = mat(i, j);
    }
  }
  return out;
}

void flat_box(const std::array<std::array<double, 3>, 3> &box, double out[9]) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      out[i * 3 + j] = box[i][j];
}

py::tuple evaluate_lj(const py::array_t<double> &positions,
                      const py::array_t<int> &atom_types,
                      const py::array_t<double> &box) {
  if (positions.ndim() != 2 || atom_types.ndim() != 1 || box.ndim() != 2) {
    throw std::invalid_argument(
        "evaluate_lj expects positions (n,3), atom_types (n,), box (3,3)");
  }
  auto pos = numpy_to_atom_matrix(positions);
  auto types = numpy_to_types(atom_types, pos.rows());
  auto cell = numpy_to_box(box);
  rgpot::LJPot pot;
  auto [energy, forces, variance] = pot(pos, types, cell);
  return py::make_tuple(energy, atom_matrix_to_numpy(forces), variance);
}

py::tuple evaluate_metatomic_dlopen(const py::array_t<double> &positions,
                                    const py::array_t<int> &atom_types,
                                    const py::array_t<double> &box,
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
  front.forceImpl(in, &out);
  return py::make_tuple(out.energy, atom_matrix_to_numpy(forces), out.variance);
}

} // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "rgpot Python bindings (LJ core + Metatomic dlopen frontend)";
  m.attr("__version__") = "2.2.1";
  m.attr("has_metatomic_dlopen") = true;

  m.def("evaluate_lj", &evaluate_lj, py::arg("positions"),
        py::arg("atom_types"), py::arg("box"));

  m.def(
      "evaluate_metatomic_dlopen", &evaluate_metatomic_dlopen,
      py::arg("positions"), py::arg("atom_types"), py::arg("box"),
      py::arg("model_path"), py::arg("engine_path") = "",
      py::arg("device") = "cpu",
      R"pbdoc(
Evaluate forces via MetatomicDlopen (dlopen libmetatomic_engine.so).

engine_path may be empty to use RGPOT_METATOMIC_ENGINE / package-bundled
libmetatomic_engine.so on the library path.
)pbdoc");

  py::class_<rgpot::LJPot>(m, "LJPot")
      .def(py::init<>())
      .def(
          "__call__",
          [](rgpot::LJPot &self, const py::array_t<double> &positions,
             const py::array_t<int> &atom_types,
             const py::array_t<double> &box) {
            auto pos = numpy_to_atom_matrix(positions);
            auto types = numpy_to_types(atom_types, pos.rows());
            auto cell = numpy_to_box(box);
            auto [energy, forces, variance] = self(pos, types, cell);
            return py::make_tuple(energy, atom_matrix_to_numpy(forces),
                                  variance);
          },
          py::arg("positions"), py::arg("atom_types"), py::arg("box"));
}
