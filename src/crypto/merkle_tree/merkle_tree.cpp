#include "crypto/merkle_tree/merkle_tree.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "crypto/hash.hpp"
#include "crypto/merkle_tree/proof_planner.hpp"

namespace swgr::crypto {
namespace {

constexpr char kLeafDomain[] = "swgr.merkle.leaf.v1";
constexpr char kNodeDomain[] = "swgr.merkle.node.v1";
constexpr std::size_t kParallelMerkleThreshold = 128;

void AppendU64Le(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU));
  }
}

std::uint64_t NextPowerOfTwo(std::uint64_t x) {
  std::uint64_t value = 1;
  while (value < x) {
    value <<= 1U;
  }
  return value;
}

std::vector<std::uint8_t> HashLeaf(HashProfile profile,
                                   std::span<const std::uint8_t> payload) {
  std::vector<std::uint8_t> framed;
  framed.reserve(payload.size() + 64);
  framed.insert(framed.end(), kLeafDomain, kLeafDomain + sizeof(kLeafDomain) - 1U);
  AppendU64Le(framed, static_cast<std::uint64_t>(payload.size()));
  framed.insert(framed.end(), payload.begin(), payload.end());
  return hash_bytes(profile, HashRole::Merkle, framed);
}

std::vector<std::uint8_t> HashParent(HashProfile profile,
                                     std::span<const std::uint8_t> left,
                                     std::span<const std::uint8_t> right) {
  std::vector<std::uint8_t> framed;
  framed.reserve(left.size() + right.size() + 64);
  framed.insert(framed.end(), kNodeDomain, kNodeDomain + sizeof(kNodeDomain) - 1U);
  AppendU64Le(framed, static_cast<std::uint64_t>(left.size()));
  framed.insert(framed.end(), left.begin(), left.end());
  AppendU64Le(framed, static_cast<std::uint64_t>(right.size()));
  framed.insert(framed.end(), right.begin(), right.end());
  return hash_bytes(profile, HashRole::Merkle, framed);
}

}  // namespace

MerkleTree::MerkleTree(std::vector<std::vector<std::uint8_t>> leaves,
                       HashProfile profile)
    : profile_(profile), leaves_(std::move(leaves)) {
  if (leaves_.empty()) {
    return;
  }

  const std::size_t padded_leaf_count = static_cast<std::size_t>(
      NextPowerOfTwo(static_cast<std::uint64_t>(leaves_.size())));
  levels_.push_back(std::vector<std::vector<std::uint8_t>>(padded_leaf_count));
  auto& leaf_level = levels_.back();
#if defined(SWGR_HAS_OPENMP)
#pragma omp parallel for if(leaves_.size() >= kParallelMerkleThreshold) schedule(static)
#endif
  for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(leaves_.size()); ++i) {
    leaf_level[static_cast<std::size_t>(i)] =
        HashLeaf(profile_, leaves_[static_cast<std::size_t>(i)]);
  }
  for (std::size_t i = leaves_.size(); i < padded_leaf_count; ++i) {
    leaf_level[i] = leaf_level[leaves_.size() - 1U];
  }

  while (levels_.back().size() > 1U) {
    const auto& current = levels_.back();
    std::vector<std::vector<std::uint8_t>> next(current.size() / 2U);
#if defined(SWGR_HAS_OPENMP)
#pragma omp parallel for if(next.size() >= kParallelMerkleThreshold) schedule(static)
#endif
    for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(next.size()); ++i) {
      const auto parent_index = static_cast<std::size_t>(i);
      next[parent_index] = HashParent(
          profile_, current[2U * parent_index], current[2U * parent_index + 1U]);
    }
    levels_.push_back(std::move(next));
  }
  root_ = levels_.back().front();
}

MerkleProof MerkleTree::open(
    const std::vector<std::uint64_t>& queried_indices) const {
  MerkleProof proof;
  if (leaves_.empty() || queried_indices.empty()) {
    return proof;
  }

  const auto plan = build_pruned_multiproof_plan(
      static_cast<std::uint64_t>(leaves_.size()), queried_indices);
  proof.queried_indices = plan.queried_indices;
  proof.leaf_payloads.reserve(proof.queried_indices.size());
  for (const auto index : proof.queried_indices) {
    proof.leaf_payloads.push_back(leaves_[static_cast<std::size_t>(index)]);
  }
  proof.sibling_hashes.reserve(
      static_cast<std::size_t>(plan.stats.unique_sibling_count));

  for (std::size_t level_index = 0; level_index < plan.levels.size(); ++level_index) {
    for (const auto sibling : plan.levels[level_index].sibling_indices) {
      proof.sibling_hashes.push_back(
          levels_[level_index][static_cast<std::size_t>(sibling)]);
    }
  }
  return proof;
}

bool MerkleTree::verify(const std::vector<std::uint8_t>& root,
                        std::size_t leaf_count, const MerkleProof& proof,
                        HashProfile profile) {
  if (leaf_count == 0) {
    return root.empty() && proof.queried_indices.empty() &&
           proof.leaf_payloads.empty() && proof.sibling_hashes.empty();
  }
  if (proof.queried_indices.size() != proof.leaf_payloads.size()) {
    return false;
  }
  if (proof.queried_indices.empty()) {
    return proof.sibling_hashes.empty();
  }

  PrunedMultiproofPlan plan;
  try {
    plan = build_pruned_multiproof_plan(
        static_cast<std::uint64_t>(leaf_count), proof.queried_indices);
  } catch (const std::exception&) {
    return false;
  }
  if (plan.queried_indices.size() != proof.queried_indices.size()) {
    return false;
  }
  if (proof.sibling_hashes.size() !=
      static_cast<std::size_t>(plan.stats.unique_sibling_count)) {
    return false;
  }

  std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> current(
      proof.queried_indices.size());
#if defined(SWGR_HAS_OPENMP)
#pragma omp parallel for if(proof.queried_indices.size() >= kParallelMerkleThreshold) schedule(static)
#endif
  for (std::ptrdiff_t i = 0;
       i < static_cast<std::ptrdiff_t>(proof.queried_indices.size()); ++i) {
    const auto index = static_cast<std::size_t>(i);
    current[index] = {proof.queried_indices[index],
                      HashLeaf(profile, proof.leaf_payloads[index])};
  }
  std::sort(current.begin(), current.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  std::size_t sibling_cursor = 0;
  for (const auto& level : plan.levels) {
    if (current.size() != level.node_indices.size()) {
      return false;
    }
    for (std::size_t i = 0; i < current.size(); ++i) {
      if (current[i].first != level.node_indices[i]) {
        return false;
      }
    }

    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> parents;
    parents.reserve(level.parent_indices.size());
    for (std::size_t i = 0; i < current.size(); ++i) {
      const auto& [index, hash] = current[i];
      const bool has_adjacent =
          i + 1U < current.size() && current[i + 1U].first == (index ^ 1ULL);

      const std::vector<std::uint8_t>* left = nullptr;
      const std::vector<std::uint8_t>* right = nullptr;
      if (has_adjacent) {
        left = (index % 2ULL == 0ULL) ? &hash : &current[i + 1U].second;
        right = (index % 2ULL == 0ULL) ? &current[i + 1U].second : &hash;
        ++i;
      } else {
        if (sibling_cursor >= proof.sibling_hashes.size()) {
          return false;
        }
        const auto& sibling = proof.sibling_hashes[sibling_cursor++];
        left = (index % 2ULL == 0ULL) ? &hash : &sibling;
        right = (index % 2ULL == 0ULL) ? &sibling : &hash;
      }

      const auto parent_index = index / 2ULL;
      if (!parents.empty() && parents.back().first == parent_index) {
        return false;
      }
      parents.push_back({parent_index, HashParent(profile, *left, *right)});
    }
    current = std::move(parents);
  }

  return sibling_cursor == proof.sibling_hashes.size() && current.size() == 1U &&
         current.front().second == root;
}

}  // namespace swgr::crypto
