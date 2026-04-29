#ifndef SWGR_WHIR_PARAMETERS_HPP_
#define SWGR_WHIR_PARAMETERS_HPP_

#include <cstdint>

#include "../parameters.hpp"
#include "whir/common.hpp"

namespace swgr::whir {

struct WhirParameters {
  std::uint64_t folding_factor = 9;
  std::uint64_t lambda_target = 128;
  swgr::HashProfile hash_profile = swgr::HashProfile::WHIR_NATIVE;
};

bool validate(const WhirParameters& params);
bool validate(const WhirPublicParameters& pp);
bool validate(const WhirParameters& params, const WhirPublicParameters& pp);
bool validate(const WhirParameters& params, const WhirCommitment& commitment);

}  // namespace swgr::whir

#endif  // SWGR_WHIR_PARAMETERS_HPP_
