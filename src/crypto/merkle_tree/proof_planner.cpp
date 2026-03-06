#include "crypto/merkle_tree/proof_planner.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace swgr::crypto {
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

}  // namespace

ProofPlanStats plan_pruned_multiproof(
    std::uint64_t tree_leaf_count, const std::vector<std::uint64_t>& queried_indices,
    std::uint64_t leaf_payload_bytes, std::uint64_t digest_bytes) {
  (void)leaf_payload_bytes;
  (void)digest_bytes;

  if (tree_leaf_count == 0) {
    throw std::invalid_argument("plan_pruned_multiproof requires leaves > 0");
  }

  std::set<std::uint64_t> current;
  for (const auto index : queried_indices) {
    if (index >= tree_leaf_count) {
      throw std::out_of_range("queried index exceeds tree size");
    }
    current.insert(index);
  }

  ProofPlanStats stats;
  stats.opened_leaf_count = static_cast<std::uint64_t>(current.size());

  if (current.empty()) {
    return stats;
  }

  const std::uint64_t padded_leaf_count = NextPowerOfTwo(tree_leaf_count);
  std::set<std::uint64_t> level = current;
  std::uint64_t level_width = padded_leaf_count;

  while (level_width > 1) {
    std::set<std::uint64_t> parents;
    for (const auto node : level) {
      const std::uint64_t sibling = node ^ 1ULL;
      if (level.find(sibling) == level.end()) {
        ++stats.unique_sibling_count;
      }
      parents.insert(node / 2);
    }
    level = std::move(parents);
    level_width /= 2;
  }

  stats.verifier_hashes = stats.unique_sibling_count;
  return stats;
}

}  // namespace swgr::crypto
