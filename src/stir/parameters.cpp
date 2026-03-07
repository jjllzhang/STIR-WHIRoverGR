#include "stir/parameters.hpp"

#include <algorithm>

#include "fri/common.hpp"
#include "stir/common.hpp"

namespace swgr::stir {
namespace {

std::uint64_t EffectiveSecurityBits(const StirParameters& params) {
  if (params.lambda_target > params.pow_bits) {
    return params.lambda_target - params.pow_bits;
  }
  return 1;
}

double HeuristicEta(swgr::SecurityMode mode, double rho) {
  const double clamped_rho = std::clamp(rho, 1.0 / 4096.0, 0.999999);
  if (mode == swgr::SecurityMode::Conservative) {
    return clamped_rho;
  }
  return std::clamp(clamped_rho / 2.0, 1.0 / 4096.0, 0.999999);
}

std::uint64_t HeuristicBaseQueryCount(const StirParameters& params, double rho) {
  const double eta = HeuristicEta(params.sec_mode, rho);
  std::uint64_t base_queries = eta >= (1.0 / 6.0) ? 2U : 1U;
  if (params.sec_mode == swgr::SecurityMode::Conservative &&
      EffectiveSecurityBits(params) >= 96 &&
      eta >= (1.0 / 6.0)) {
    base_queries = 3;
  }
  return base_queries;
}

}  // namespace

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

std::vector<std::uint64_t> resolve_query_repetitions(
    const StirParameters& params, const StirInstance& instance) {
  const std::size_t rounds = folding_round_count(instance, params);
  if (!params.query_repetitions.empty()) {
    return swgr::fri::query_schedule(rounds, params.query_repetitions);
  }

  std::vector<std::uint64_t> schedule;
  schedule.reserve(rounds);
  std::uint64_t current_domain_size = instance.domain.size();
  std::uint64_t current_degree_bound = instance.claimed_degree;
  for (std::size_t round_index = 0; round_index < rounds; ++round_index) {
    const double rho = static_cast<double>(current_degree_bound + 1U) /
                       static_cast<double>(current_domain_size);
    const std::uint64_t bundle_count =
        current_domain_size / params.virtual_fold_factor;
    const auto next_degree_bound =
        folded_degree_bound(current_degree_bound, params.virtual_fold_factor);
    const std::uint64_t degree_budget =
        next_degree_bound + 1U > params.ood_samples
            ? next_degree_bound + 1U - params.ood_samples
            : 1U;
    const std::uint64_t base_queries =
        HeuristicBaseQueryCount(params, rho);
    const std::uint64_t decayed_queries =
        base_queries > round_index ? base_queries - round_index : 1U;
    const std::uint64_t query_count = std::min(
        decayed_queries,
        std::max<std::uint64_t>(1, std::min(bundle_count, degree_budget)));
    schedule.push_back(query_count);
    current_domain_size /= params.shift_power;
    current_degree_bound = next_degree_bound;
  }
  return schedule;
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
    const auto schedule = resolve_query_repetitions(params, instance);
    if (schedule.size() != rounds) {
      return false;
    }

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
