#ifndef SWGR_WHIR_PARAMETERS_HPP_
#define SWGR_WHIR_PARAMETERS_HPP_

#include <cstdint>

#include "../parameters.hpp"

namespace swgr::whir {

struct WhirParameters {
  std::uint64_t folding_factor = 9;
  std::uint64_t lambda_target = 128;
  swgr::HashProfile hash_profile = swgr::HashProfile::WHIR_NATIVE;
};

bool validate(const WhirParameters& params);

}  // namespace swgr::whir

#endif  // SWGR_WHIR_PARAMETERS_HPP_
