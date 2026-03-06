#ifndef SWGR_CRYPTO_MERKLE_TREE_PROOF_PLANNER_HPP_
#define SWGR_CRYPTO_MERKLE_TREE_PROOF_PLANNER_HPP_

#include <cstdint>
#include <vector>

namespace swgr::crypto {

struct ProofPlanStats {
  std::uint64_t opened_leaf_count = 0;
  std::uint64_t unique_sibling_count = 0;
  std::uint64_t verifier_hashes = 0;
};

ProofPlanStats plan_pruned_multiproof(
    std::uint64_t tree_leaf_count, const std::vector<std::uint64_t>& queried_indices,
    std::uint64_t leaf_payload_bytes, std::uint64_t digest_bytes);

}  // namespace swgr::crypto

#endif  // SWGR_CRYPTO_MERKLE_TREE_PROOF_PLANNER_HPP_
