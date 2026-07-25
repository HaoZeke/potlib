// MIT License
// Copyright 2023--present rgpot developers

#include <array>
#include <stdexcept>
#include <string>

#include "rgpot/fortran/FortranPots.hpp"

namespace rgpot {
namespace fortranpots {

void raise(const char *pot, int status) {
  std::array<char, 512> buffer{};
  const int written =
      rgpot_fortran_last_error(buffer.data(), static_cast<int>(buffer.size()));

  std::string message = std::string(pot) + " potential failed (status " +
                        std::to_string(status) + ")";
  if (written > 0) {
    message += ": ";
    message.append(buffer.data(), static_cast<std::size_t>(written));
  }
  throw std::runtime_error(message);
}

} // namespace fortranpots
} // namespace rgpot
