#ifndef SWGR_PARAMETERS_HPP_
#define SWGR_PARAMETERS_HPP_

#include <cstdint>
#include <string>

namespace swgr {

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

}  // namespace swgr

#endif  // SWGR_PARAMETERS_HPP_
