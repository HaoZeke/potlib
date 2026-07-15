// MIT License
// Copyright 2023--present rgpot developers
//
// Thin pybind11 surface for the always-on core (Lennard-Jones).
// Optional heavy backends (RPC, Metatomic, NWChem, …) are not required.

#include <array>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "rgpot/LennardJones/LJPot.hpp"
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

} // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "rgpot core Python bindings (Lennard-Jones and related)";
  m.attr("__version__") = "2.2.1";

  m.def("evaluate_lj", &evaluate_lj, py::arg("positions"),
        py::arg("atom_types"), py::arg("box"),
        R"pbdoc(
Evaluate the always-on shifted 12-6 Lennard-Jones potential.

Parameters
----------
positions : ndarray, shape (n_atoms, 3)
    Cartesian coordinates (Angstrom).
atom_types : ndarray, shape (n_atoms,), dtype int
    Integer type ids (LJ uses type for identity; 0 is fine for pure LJ).
box : ndarray, shape (3, 3)
    Cell matrix (row vectors).

Returns
-------
energy : float
    Potential energy.
forces : ndarray, shape (n_atoms, 3)
    Forces.
variance : float
    Unused for LJ (always 0).
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
