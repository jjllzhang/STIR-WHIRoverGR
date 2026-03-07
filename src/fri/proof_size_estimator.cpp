#include "fri/proof_size_estimator.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <utility>

#include "crypto/hash.hpp"
#include "crypto/merkle_tree/proof_planner.hpp"

namespace swgr::fri {

FriProofSizeEstimator::FriProofSizeEstimator(FriParameters params)
    : params_(std::move(params)) {}

swgr::EstimateResult FriProofSizeEstimator::estimate(
    const FriInstance& instance) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument(
        "fri::FriProofSizeEstimator::estimate received invalid instance");
  }

  swgr::EstimateResult result;
  const auto& ctx = instance.domain.context();
  const std::size_t fold_rounds =
      folding_round_count(instance, params_.fold_factor, params_.stop_degree);
  const auto query_rounds = resolve_query_rounds_metadata(params_, instance);
  const std::uint64_t digest_bytes =
      static_cast<std::uint64_t>(swgr::crypto::digest_bytes(params_.hash_profile));
  const std::uint64_t leaf_payload_bytes =
      static_cast<std::uint64_t>(params_.fold_factor) *
      static_cast<std::uint64_t>(ctx.elem_bytes());

  Domain current_domain = instance.domain;
  std::uint64_t current_degree = instance.claimed_degree;
  std::vector<std::string> round_entries;
  round_entries.reserve(fold_rounds);

  for (std::size_t round_index = 0; round_index < fold_rounds; ++round_index) {
    const auto& round_meta = query_rounds[round_index];
    const std::uint64_t bundle_count = round_meta.bundle_count;
    auto round_seed = commit_oracle(ctx, {ctx.one(), current_domain.root()});
    round_seed.push_back(static_cast<std::uint8_t>(round_index));
    const auto queried_indices = derive_query_positions(
        round_seed, static_cast<std::uint64_t>(round_index), bundle_count,
        round_meta.effective_query_count);
    const auto stats = swgr::crypto::plan_pruned_multiproof(
        bundle_count, queried_indices, leaf_payload_bytes, digest_bytes);

    const std::uint64_t round_argument_bytes =
        digest_bytes +
        stats.opened_leaf_count * leaf_payload_bytes +
        stats.unique_sibling_count * digest_bytes;
    result.argument_bytes += round_argument_bytes;
    result.verifier_hashes += stats.verifier_hashes;

    std::ostringstream oss;
    oss << "{\"round\":" << round_index
        << ",\"domain_size\":" << current_domain.size()
        << ",\"bundle_count\":" << bundle_count
        << ",\"query_count\":" << round_meta.effective_query_count
        << ",\"requested_query_count\":" << round_meta.requested_query_count
        << ",\"effective_query_count\":" << round_meta.effective_query_count
        << ",\"cap_applied\":"
        << (round_meta.cap_applied ? "true" : "false")
        << ",\"opened_leaf_count\":" << stats.opened_leaf_count
        << ",\"unique_sibling_count\":" << stats.unique_sibling_count
        << ",\"round_argument_bytes\":" << round_argument_bytes << "}";
    round_entries.push_back(oss.str());

    current_domain = current_domain.pow_map(params_.fold_factor);
    current_degree /= params_.fold_factor;
  }

  const std::uint64_t final_polynomial_bytes =
      static_cast<std::uint64_t>(current_degree + 1) *
      static_cast<std::uint64_t>(ctx.elem_bytes());
  result.argument_bytes += final_polynomial_bytes;
  result.round_breakdown_json =
      estimate_breakdown_json(round_entries, final_polynomial_bytes);
  return result;
}

}  // namespace swgr::fri
