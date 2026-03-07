#ifndef SWGR_STIR_PARAMETERS_HPP_
#define SWGR_STIR_PARAMETERS_HPP_

#include <cstdint>
#include <vector>

#include "../parameters.hpp"

namespace swgr::stir {

struct StirInstance;

struct StirParameters {
  std::uint64_t virtual_fold_factor = 9;
  std::uint64_t shift_power = 3;
  std::uint64_t ood_samples = 2;
  std::vector<std::uint64_t> query_repetitions;
  std::uint64_t stop_degree = 3;
  std::uint64_t lambda_target = 128;
  std::uint64_t pow_bits = 0;
  swgr::SecurityMode sec_mode = swgr::SecurityMode::ConjectureCapacity;
  swgr::HashProfile hash_profile = swgr::HashProfile::STIR_NATIVE;
};

bool validate(const StirParameters& params);
bool validate(const StirParameters& params, const StirInstance& instance);
std::vector<std::uint64_t> resolve_query_repetitions(
    const StirParameters& params, const StirInstance& instance);

}  // namespace swgr::stir

#endif  // SWGR_STIR_PARAMETERS_HPP_
