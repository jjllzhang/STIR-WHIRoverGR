#include "utils.hpp"

#include <stdexcept>
#include <string>

namespace swgr {

[[noreturn]] void throw_unimplemented(std::string_view feature) {
  throw std::logic_error("Not implemented yet: " + std::string(feature));
}

bool is_power_of(std::uint64_t value, std::uint64_t base) {
  if (base < 2 || value == 0) {
    return false;
  }

  std::uint64_t current = value;
  while (current % base == 0) {
    current /= base;
  }
  return current == 1;
}

}  // namespace swgr
