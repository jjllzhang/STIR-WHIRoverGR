#ifndef STIR_WHIR_GR_CRYPTO_MERKLE_TREE_PROOF_PLANNER_HPP_
#define STIR_WHIR_GR_CRYPTO_MERKLE_TREE_PROOF_PLANNER_HPP_

#include <cstdint>
#include <vector>

namespace stir_whir_gr::crypto {

struct ProofPlanStats {
  std::uint64_t opened_leaf_count = 0;
  std::uint64_t unique_sibling_count = 0;
  std::uint64_t verifier_hashes = 0;
};

struct ProofPlanLevel {
  std::vector<std::uint64_t> node_indices;
  std::vector<std::uint64_t> sibling_indices;
  std::vector<std::uint64_t> parent_indices;
};

struct PrunedMultiproofPlan {
  std::uint64_t tree_leaf_count = 0;
  std::uint64_t padded_leaf_count = 0;
  std::vector<std::uint64_t> queried_indices;
  std::vector<ProofPlanLevel> levels;
  ProofPlanStats stats;
};

PrunedMultiproofPlan build_pruned_multiproof_plan(
    std::uint64_t tree_leaf_count,
    const std::vector<std::uint64_t>& queried_indices);

ProofPlanStats plan_pruned_multiproof(
    std::uint64_t tree_leaf_count, const std::vector<std::uint64_t>& queried_indices,
    std::uint64_t leaf_payload_bytes, std::uint64_t digest_bytes);

}  // namespace stir_whir_gr::crypto

#endif  // STIR_WHIR_GR_CRYPTO_MERKLE_TREE_PROOF_PLANNER_HPP_
