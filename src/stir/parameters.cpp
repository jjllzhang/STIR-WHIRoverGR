#include "stir/parameters.hpp"

#include "fri/common.hpp"
#include "stir/common.hpp"

namespace swgr::stir {

bool validate(const StirParameters& params) {
  if (params.virtual_fold_factor != 9 || params.shift_power != 3 ||
      params.stop_degree == 0 || params.ood_samples == 0) {
    return false;
  }

  for (const auto query_count : params.query_repetitions) {
    if (query_count == 0) {
      return false;
    }
  }
  return true;
}

bool validate(const StirParameters& params, const StirInstance& instance) {
  if (!validate(params) || instance.domain.size() == 0 ||
      instance.claimed_degree >= instance.domain.size()) {
    return false;
  }

  try {
    Domain current_domain = instance.domain;
    std::uint64_t current_degree_bound = instance.claimed_degree;
    const std::size_t rounds = folding_round_count(instance, params);
    const auto schedule =
        swgr::fri::query_schedule(rounds, params.query_repetitions);

    for (std::size_t round_index = 0; round_index < rounds; ++round_index) {
      const Domain folded_domain =
          current_domain.pow_map(params.virtual_fold_factor);
      const Domain shift_domain = current_domain.scale_offset(params.shift_power);
      if (!folded_domain.disjoint_with(shift_domain)) {
        return false;
      }

      const auto next_degree_bound = folded_degree_bound(
          current_degree_bound, params.virtual_fold_factor);
      if (params.ood_samples + schedule[round_index] > next_degree_bound + 1) {
        return false;
      }

      current_domain = shift_domain;
      current_degree_bound = next_degree_bound;
    }
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace swgr::stir
