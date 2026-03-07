#include "fri/parameters.hpp"

#include <algorithm>

#include "utils.hpp"

namespace swgr::fri {
namespace {

std::uint64_t EffectiveSecurityBits(const FriParameters& params) {
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

std::uint64_t HeuristicBaseQueryCount(const FriParameters& params, double rho) {
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

bool validate(const FriParameters& params) {
  if ((params.fold_factor != 3 && params.fold_factor != 9) ||
      params.stop_degree == 0) {
    return false;
  }

  for (const auto query_count : params.query_repetitions) {
    if (query_count == 0) {
      return false;
    }
  }
  return true;
}

QueryRoundMetadata resolve_query_round_metadata(std::uint64_t requested_count,
                                                std::uint64_t bundle_count) {
  QueryRoundMetadata metadata;
  metadata.requested_query_count = requested_count;
  metadata.bundle_count = bundle_count;
  metadata.effective_query_count = std::min(requested_count, bundle_count);
  metadata.cap_applied = requested_count > bundle_count;
  return metadata;
}

std::vector<QueryRoundMetadata> resolve_query_rounds_metadata(
    const FriParameters& params, const FriInstance& instance) {
  const std::size_t rounds =
      folding_round_count(instance, params.fold_factor, params.stop_degree);
  const auto requested_schedule =
      params.query_repetitions.empty()
          ? std::vector<std::uint64_t>()
          : query_schedule(rounds, params.query_repetitions);
  std::vector<QueryRoundMetadata> metadata;
  metadata.reserve(rounds);

  std::uint64_t current_domain_size = instance.domain.size();
  std::uint64_t current_degree_bound = instance.claimed_degree;
  for (std::size_t round_index = 0; round_index < rounds; ++round_index) {
    std::uint64_t requested_count = 0;
    if (!requested_schedule.empty()) {
      requested_count = requested_schedule[round_index];
    } else {
      const double rho = static_cast<double>(current_degree_bound + 1U) /
                         static_cast<double>(current_domain_size);
      const std::uint64_t base_queries =
          HeuristicBaseQueryCount(params, rho);
      requested_count =
          base_queries > round_index ? base_queries - round_index : 1U;
    }
    const std::uint64_t bundle_count = current_domain_size / params.fold_factor;
    metadata.push_back(
        resolve_query_round_metadata(requested_count, bundle_count));
    current_domain_size /= params.fold_factor;
    current_degree_bound /= params.fold_factor;
  }
  return metadata;
}

std::vector<std::uint64_t> resolve_query_repetitions(
    const FriParameters& params, const FriInstance& instance) {
  const auto metadata = resolve_query_rounds_metadata(params, instance);
  std::vector<std::uint64_t> schedule;
  schedule.reserve(metadata.size());
  for (const auto& round : metadata) {
    schedule.push_back(round.effective_query_count);
  }
  return schedule;
}

bool validate(const FriParameters& params, const FriInstance& instance) {
  if (!validate(params)) {
    return false;
  }
  if (instance.domain.size() == 0) {
    return false;
  }
  if (!swgr::is_power_of(instance.domain.size(), 3)) {
    return false;
  }
  if (instance.claimed_degree >= instance.domain.size()) {
    return false;
  }

  std::uint64_t current_domain_size = instance.domain.size();
  std::uint64_t current_degree_bound = instance.claimed_degree;
  try {
    const auto schedule = resolve_query_repetitions(params, instance);
    if (schedule.size() !=
        folding_round_count(instance, params.fold_factor, params.stop_degree)) {
      return false;
    }
  } catch (...) {
    return false;
  }
  while (current_degree_bound > params.stop_degree) {
    if (current_domain_size % params.fold_factor != 0) {
      return false;
    }
    current_domain_size /= params.fold_factor;
    current_degree_bound /= params.fold_factor;
  }
  return true;
}

}  // namespace swgr::fri
