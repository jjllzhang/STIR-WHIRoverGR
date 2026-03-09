#include "stir/parameters.hpp"

#include <algorithm>

#include "fri/common.hpp"
#include "soundness/configurator.hpp"
#include "stir/common.hpp"

namespace swgr::stir {

bool validate(const StirParameters& params) {
  if (params.virtual_fold_factor != 9 || params.shift_power != 3 ||
      params.stop_degree == 0 || params.ood_samples == 0) {
    return false;
  }

  return swgr::soundness::validate_manual_queries(params.query_repetitions);
}

std::vector<std::uint64_t> resolve_query_repetitions(
    const StirParameters& params, const StirInstance& instance) {
  const auto metadata = resolve_query_schedule_metadata(params, instance);
  std::vector<std::uint64_t> schedule;
  schedule.reserve(metadata.size());
  for (const auto& round : metadata) {
    schedule.push_back(round.effective_query_count);
  }
  return schedule;
}

std::vector<RoundQueryScheduleMetadata> resolve_query_schedule_metadata(
    const StirParameters& params, const StirInstance& instance) {
  const std::size_t rounds = folding_round_count(instance, params);
  const auto requested_schedule =
      params.query_repetitions.empty()
          ? std::vector<std::uint64_t>()
          : swgr::fri::query_schedule(rounds, params.query_repetitions);

  std::vector<RoundQueryScheduleMetadata> metadata;
  metadata.reserve(rounds);
  std::uint64_t current_domain_size = instance.domain.size();
  std::uint64_t current_degree_bound = instance.claimed_degree;
  for (std::size_t round_index = 0; round_index < rounds; ++round_index) {
    std::uint64_t requested_query_count = 0;
    if (!requested_schedule.empty()) {
      requested_query_count = requested_schedule[round_index];
    } else {
      const double rho = static_cast<double>(current_degree_bound + 1U) /
                         static_cast<double>(current_domain_size);
      requested_query_count = swgr::soundness::auto_query_count_for_round(
          params.sec_mode, params.lambda_target, params.pow_bits, rho,
          round_index);
    }

    const std::uint64_t bundle_count =
        current_domain_size / params.virtual_fold_factor;
    const auto next_degree_bound =
        folded_degree_bound(current_degree_bound, params.virtual_fold_factor);
    const std::uint64_t degree_budget =
        next_degree_bound + 1U > params.ood_samples
            ? next_degree_bound + 1U - params.ood_samples
            : 0U;
    const std::uint64_t effective_query_count =
        std::min(requested_query_count, std::min(bundle_count, degree_budget));

    metadata.push_back(RoundQueryScheduleMetadata{
        .requested_query_count = requested_query_count,
        .effective_query_count = effective_query_count,
        .bundle_count = bundle_count,
        .degree_budget = degree_budget,
        .cap_applied = (effective_query_count != requested_query_count),
    });
    current_domain_size /= params.shift_power;
    current_degree_bound = next_degree_bound;
  }
  return metadata;
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
    const auto schedule_metadata =
        resolve_query_schedule_metadata(params, instance);
    if (schedule_metadata.size() != rounds) {
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
      const auto& round = schedule_metadata[round_index];
      if (round.effective_query_count == 0 ||
          round.effective_query_count > round.bundle_count) {
        return false;
      }
      if (params.ood_samples + round.effective_query_count >
          next_degree_bound + 1) {
        return false;
      }

      current_domain = shift_domain;
      current_degree_bound = next_degree_bound;
    }

    if (current_domain.size() % params.virtual_fold_factor != 0) {
      return false;
    }
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace swgr::stir
