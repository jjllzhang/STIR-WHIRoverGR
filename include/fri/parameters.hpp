#ifndef SWGR_FRI_PARAMETERS_HPP_
#define SWGR_FRI_PARAMETERS_HPP_

#include <cstdint>
#include <vector>

#include "../parameters.hpp"
#include "fri/common.hpp"

namespace swgr::fri {

struct FriParameters {
  std::uint64_t fold_factor = 3;
  std::uint64_t stop_degree = 3;
  std::uint64_t ood_samples = 2;
  std::vector<std::uint64_t> query_repetitions;
  std::uint64_t lambda_target = 128;
  std::uint64_t pow_bits = 0;
  swgr::SecurityMode sec_mode = swgr::SecurityMode::ConjectureCapacity;
  swgr::HashProfile hash_profile = swgr::HashProfile::STIR_NATIVE;
};

bool validate(const FriParameters& params);
bool validate(const FriParameters& params, const FriInstance& instance);

}  // namespace swgr::fri

#endif  // SWGR_FRI_PARAMETERS_HPP_
