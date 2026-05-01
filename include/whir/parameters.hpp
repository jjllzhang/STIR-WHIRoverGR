#ifndef STIR_WHIR_GR_WHIR_PARAMETERS_HPP_
#define STIR_WHIR_GR_WHIR_PARAMETERS_HPP_

#include <cstdint>

#include "../parameters.hpp"
#include "whir/common.hpp"

namespace stir_whir_gr::whir {

struct WhirParameters {
  std::uint64_t folding_factor = 9;
  std::uint64_t lambda_target = 128;
  stir_whir_gr::HashProfile hash_profile = stir_whir_gr::HashProfile::WHIR_NATIVE;
};

bool validate(const WhirParameters& params);
bool validate(const WhirPublicParameters& pp);
bool validate(const WhirParameters& params, const WhirPublicParameters& pp);
bool validate(const WhirParameters& params, const WhirCommitment& commitment);

}  // namespace stir_whir_gr::whir

#endif  // STIR_WHIR_GR_WHIR_PARAMETERS_HPP_
