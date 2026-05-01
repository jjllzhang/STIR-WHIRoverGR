#include "fri/parameters.hpp"

#include "utils.hpp"

namespace stir_whir_gr::fri {

bool validate(const FriParameters& params) {
  if ((params.fold_factor != 3 && params.fold_factor != 9) ||
      params.stop_degree == 0 || params.repetition_count == 0) {
    return false;
  }
  return true;
}

std::vector<QueryRoundMetadata> resolve_query_rounds_metadata(
    const FriParameters& params, const FriInstance& instance) {
  const std::size_t rounds =
      folding_round_count(instance, params.fold_factor, params.stop_degree);
  std::vector<QueryRoundMetadata> metadata;
  metadata.reserve(rounds);

  std::uint64_t current_domain_size = instance.domain.size();
  for (std::size_t round_index = 0; round_index < rounds; ++round_index) {
    const std::uint64_t bundle_count = current_domain_size / params.fold_factor;
    metadata.push_back(QueryRoundMetadata{
        .query_chain_count = params.repetition_count,
        .fresh_query_count = params.repetition_count,
        .bundle_count = bundle_count,
        .carries_previous_queries = false,
    });
    current_domain_size /= params.fold_factor;
  }
  return metadata;
}

std::uint64_t terminal_query_chain_count(const FriParameters& params) {
  return params.repetition_count;
}

bool validate(const FriParameters& params, const FriInstance& instance) {
  if (!validate(params)) {
    return false;
  }
  if (instance.domain.size() == 0) {
    return false;
  }
  if (!stir_whir_gr::is_power_of(instance.domain.size(), 3)) {
    return false;
  }
  if (instance.claimed_degree >= instance.domain.size()) {
    return false;
  }

  std::uint64_t current_domain_size = instance.domain.size();
  std::uint64_t current_degree_bound = instance.claimed_degree;
  try {
    if (resolve_query_rounds_metadata(params, instance).size() !=
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

bool validate(const FriParameters& params, const FriCommitment& commitment) {
  if (!validate(params)) {
    return false;
  }
  if (!commitment_domain_supported(commitment)) {
    return false;
  }
  if (commitment.oracle_root.empty()) {
    return false;
  }
  return validate(params, FriInstance{
                              .domain = commitment.domain,
                              .claimed_degree = commitment.degree_bound,
                          });
}

bool validate(const FriCommitment& commitment, const FriOpeningClaim& claim) {
  return !commitment.oracle_root.empty() &&
         commitment_domain_supported(commitment) &&
         opening_point_valid(commitment, claim.alpha);
}

}  // namespace stir_whir_gr::fri
