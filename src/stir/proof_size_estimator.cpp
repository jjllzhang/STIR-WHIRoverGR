#include "stir/proof_size_estimator.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "crypto/hash.hpp"
#include "crypto/merkle_tree/proof_planner.hpp"
#include "fri/common.hpp"

namespace swgr::stir {

StirProofSizeEstimator::StirProofSizeEstimator(StirParameters params)
    : params_(std::move(params)) {}

swgr::EstimateResult StirProofSizeEstimator::estimate(
    const StirInstance& instance) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument(
        "stir::StirProofSizeEstimator::estimate received invalid instance");
  }

  swgr::EstimateResult result;
  const auto& ctx = instance.domain.context();
  const std::size_t round_count = folding_round_count(instance, params_);
  const auto query_metadata = resolve_query_schedule_metadata(params_, instance);
  const std::uint64_t digest_bytes =
      static_cast<std::uint64_t>(swgr::crypto::digest_bytes(params_.hash_profile));
  const std::uint64_t elem_bytes =
      static_cast<std::uint64_t>(ctx.elem_bytes());
  const std::uint64_t fold_leaf_payload_bytes =
      params_.virtual_fold_factor * elem_bytes;

  Domain current_domain = instance.domain;
  std::uint64_t current_degree_bound = instance.claimed_degree;
  std::vector<std::string> round_entries;
  round_entries.reserve(round_count);

  for (std::size_t round_index = 0; round_index < round_count; ++round_index) {
    const auto& query_round = query_metadata[round_index];
    const std::uint64_t bundle_count = query_round.bundle_count;
    const std::uint64_t effective_query_count =
        query_round.effective_query_count;
    const auto seed_material = swgr::fri::commit_oracle(
        ctx, {ctx.one(), current_domain.offset(), current_domain.root()});
    const auto queried_indices = derive_unique_positions(
        seed_material, static_cast<std::uint64_t>(round_index), bundle_count,
        effective_query_count);
    const auto proof_plan = swgr::crypto::plan_pruned_multiproof(
        bundle_count, queried_indices, fold_leaf_payload_bytes, digest_bytes);
    const std::uint64_t fold_opening_payload_bytes =
        proof_plan.opened_leaf_count * fold_leaf_payload_bytes;
    const std::uint64_t single_point_opening_bytes =
        (params_.ood_samples + effective_query_count) * elem_bytes;
    const std::uint64_t round_argument_bytes =
        digest_bytes + fold_opening_payload_bytes +
        proof_plan.unique_sibling_count * digest_bytes +
        single_point_opening_bytes;

    result.argument_bytes += round_argument_bytes;
    result.verifier_hashes += proof_plan.verifier_hashes;

    std::ostringstream oss;
    oss << "{\"round\":" << round_index
        << ",\"domain_size\":" << current_domain.size()
        << ",\"bundle_count\":" << bundle_count
        << ",\"degree_budget\":" << query_round.degree_budget
        << ",\"requested_query_count\":" << query_round.requested_query_count
        << ",\"effective_query_count\":" << effective_query_count
        << ",\"cap_applied\":" << (query_round.cap_applied ? "true" : "false")
        << ",\"query_count\":" << effective_query_count
        << ",\"ood_samples\":" << params_.ood_samples
        << ",\"opened_leaf_count\":" << proof_plan.opened_leaf_count
        << ",\"unique_sibling_count\":" << proof_plan.unique_sibling_count
        << ",\"fold_opening_payload_bytes\":" << fold_opening_payload_bytes
        << ",\"single_point_opening_bytes\":" << single_point_opening_bytes
        << ",\"round_argument_bytes\":" << round_argument_bytes
        << ",\"fill_used\":false}";
    round_entries.push_back(oss.str());

    current_domain = current_domain.scale_offset(params_.shift_power);
    current_degree_bound =
        folded_degree_bound(current_degree_bound, params_.virtual_fold_factor);
  }

  const std::uint64_t final_polynomial_bytes =
      (current_degree_bound + 1U) * elem_bytes;
  result.argument_bytes += final_polynomial_bytes;
  result.round_breakdown_json =
      swgr::fri::estimate_breakdown_json(round_entries, final_polynomial_bytes);
  return result;
}

}  // namespace swgr::stir
