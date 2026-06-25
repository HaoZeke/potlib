#pragma once
// Minimal pybind11 stub for compile-only Psi4 headers (no real bindings).
// Psi4 C++ headers include psi4/pybind11.h; engine never uses Python.

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pybind11 {

class object {
public:
  object() = default;
  object(std::nullptr_t) {}
  explicit operator bool() const { return false; }
  template <typename T> T cast() const {
    throw std::runtime_error("pybind11 stub: cast() not available (no Python)");
  }
};

class handle : public object {};
class module_ : public object {};
class dict : public object {};
class list : public object {};
class str : public object {
public:
  str() = default;
  str(const char *) {}
  str(const std::string &) {}
};

inline std::size_t len(const object &) { return 0; }

template <typename T> class class_ {};
template <typename T> class enum_ {};

#define PYBIND11_MODULE(name, m)                                               \
  static void pybind11_init_##name();                                          \
  static void pybind11_init_##name()

#define PYBIND11_DECLARE_HOLDER_TYPE(type, holder)                             \
  namespace pybind11 {                                                          \
  namespace detail {                                                            \
  template <typename type> class type_caster<holder> {};                       \
  }                                                                            \
  }

} // namespace pybind11

namespace py = pybind11;
