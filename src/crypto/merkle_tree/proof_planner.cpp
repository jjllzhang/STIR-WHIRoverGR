#include "crypto/merkle_tree/proof_planner.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace stir_whir_gr::crypto {
namespace {

std::uint64_t NextPowerOfTwo(std::uint64_t x) {
  if (x <= 1) {
    return 1;
  }
  std::uint64_t value = 1;
  while (value < x) {
    value <<= 1;
  }
  return value;
}

std::vector<std::uint64_t> SortAndValidateUnique(
    std::uint64_t tree_leaf_count,
    const std::vector<std::uint64_t>& queried_indices) {
  std::vector<std::uint64_t> unique = queried_indices;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  for (const auto index : unique) {
    if (index >= tree_leaf_count) {
      throw std::out_of_range("Merkle query index exceeds leaf count");
    }
  }
  return unique;
}

}  // namespace

PrunedMultiproofPlan build_pruned_multiproof_plan(
    std::uint64_t tree_leaf_count,
    const std::vector<std::uint64_t>& queried_indices) {
  if (tree_leaf_count == 0) {
    throw std::invalid_argument("build_pruned_multiproof_plan requires leaves > 0");
  }

  PrunedMultiproofPlan plan;
  plan.tree_leaf_count = tree_leaf_count;
  plan.padded_leaf_count = NextPowerOfTwo(tree_leaf_count);
  plan.queried_indices = SortAndValidateUnique(tree_leaf_count, queried_indices);
  plan.stats.opened_leaf_count =
      static_cast<std::uint64_t>(plan.queried_indices.size());

  if (plan.queried_indices.empty()) {
    return plan;
  }

  std::vector<std::uint64_t> current = plan.queried_indices;
  std::uint64_t level_width = plan.padded_leaf_count;
  while (level_width > 1) {
    ProofPlanLevel level;
    level.node_indices = current;
    level.sibling_indices.reserve((current.size() + 1U) / 2U);
    level.parent_indices.reserve((current.size() + 1U) / 2U);

    for (std::size_t i = 0; i < current.size();) {
      const auto node = current[i];
      const bool has_paired_sibling =
          i + 1U < current.size() && (node % 2ULL == 0ULL) &&
          current[i + 1U] == node + 1ULL;
      if (!has_paired_sibling) {
        level.sibling_indices.push_back(node ^ 1ULL);
      }
      level.parent_indices.push_back(node / 2ULL);
      i += has_paired_sibling ? 2U : 1U;
    }

    plan.stats.unique_sibling_count +=
        static_cast<std::uint64_t>(level.sibling_indices.size());
    current = level.parent_indices;
    plan.levels.push_back(std::move(level));
    level_width /= 2ULL;
  }

  plan.stats.verifier_hashes = plan.stats.unique_sibling_count;
  return plan;
}

ProofPlanStats plan_pruned_multiproof(
    std::uint64_t tree_leaf_count, const std::vector<std::uint64_t>& queried_indices,
    std::uint64_t leaf_payload_bytes, std::uint64_t digest_bytes) {
  (void)leaf_payload_bytes;
  (void)digest_bytes;
  return build_pruned_multiproof_plan(tree_leaf_count, queried_indices).stats;
}

}  // namespace stir_whir_gr::crypto
