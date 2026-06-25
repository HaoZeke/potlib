#pragma once
// Psi4's psi4/pybind11.h shim — compile-only stub, no Python runtime.
#include <memory>
#include <pybind11/pybind11.h>
namespace py = pybind11;
PYBIND11_DECLARE_HOLDER_TYPE(T, std::shared_ptr<T>)
