#ifndef STIR_WHIR_GR_PARAMETERS_HPP_
#define STIR_WHIR_GR_PARAMETERS_HPP_

#include <cstdint>
#include <string>

namespace stir_whir_gr {

enum class SecurityMode {
  ConjectureCapacity,
  Conservative,
};

enum class HashProfile {
  STIR_NATIVE,
  WHIR_NATIVE,
};

struct WorkloadParameters {
  std::uint64_t domain_size = 0;
  std::uint64_t degree = 0;
};

std::string to_string(SecurityMode mode);
std::string to_string(HashProfile profile);

}  // namespace stir_whir_gr

#endif  // STIR_WHIR_GR_PARAMETERS_HPP_
