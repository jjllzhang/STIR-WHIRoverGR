#include "crypto/merkle_tree/merkle_tree.hpp"

#include <algorithm>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "crypto/hash.hpp"

namespace swgr::crypto {
namespace {

constexpr char kLeafDomain[] = "swgr.merkle.leaf.v1";
constexpr char kNodeDomain[] = "swgr.merkle.node.v1";

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

std::vector<std::uint64_t> SortAndValidateUnique(
    const std::vector<std::uint64_t>& queried_indices, std::size_t leaf_count) {
  std::vector<std::uint64_t> unique = queried_indices;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  for (const auto index : unique) {
    if (index >= leaf_count) {
      throw std::out_of_range("Merkle query index exceeds leaf count");
    }
  }
  return unique;
}

}  // namespace

MerkleTree::MerkleTree(std::vector<std::vector<std::uint8_t>> leaves,
                       HashProfile profile)
    : profile_(profile), leaves_(std::move(leaves)) {
  if (leaves_.empty()) {
    return;
  }

  levels_.push_back({});
  levels_.back().reserve(NextPowerOfTwo(static_cast<std::uint64_t>(leaves_.size())));
  for (const auto& leaf : leaves_) {
    levels_.back().push_back(HashLeaf(profile_, leaf));
  }
  while (levels_.back().size() & (levels_.back().size() - 1U)) {
    levels_.back().push_back(levels_.back().back());
  }

  while (levels_.back().size() > 1U) {
    const auto& current = levels_.back();
    std::vector<std::vector<std::uint8_t>> next;
    next.reserve(current.size() / 2U);
    for (std::size_t i = 0; i < current.size(); i += 2U) {
      next.push_back(HashParent(profile_, current[i], current[i + 1U]));
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

  proof.queried_indices = SortAndValidateUnique(queried_indices, leaves_.size());
  proof.leaf_payloads.reserve(proof.queried_indices.size());
  for (const auto index : proof.queried_indices) {
    proof.leaf_payloads.push_back(leaves_[static_cast<std::size_t>(index)]);
  }

  std::set<std::uint64_t> current(proof.queried_indices.begin(),
                                  proof.queried_indices.end());
  for (std::size_t level_index = 0; level_index + 1U < levels_.size();
       ++level_index) {
    std::set<std::uint64_t> parents;
    for (const auto node : current) {
      const auto sibling = node ^ 1ULL;
      if (current.find(sibling) == current.end()) {
        proof.sibling_hashes.push_back(
            levels_[level_index][static_cast<std::size_t>(sibling)]);
      }
      parents.insert(node / 2ULL);
    }
    current = std::move(parents);
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

  std::vector<std::uint64_t> sorted = proof.queried_indices;
  std::sort(sorted.begin(), sorted.end());
  if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
    return false;
  }
  for (const auto index : sorted) {
    if (index >= leaf_count) {
      return false;
    }
  }

  std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> current;
  current.reserve(proof.queried_indices.size());
  for (std::size_t i = 0; i < proof.queried_indices.size(); ++i) {
    current.push_back(
        {proof.queried_indices[i], HashLeaf(profile, proof.leaf_payloads[i])});
  }
  std::sort(current.begin(), current.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  std::size_t sibling_cursor = 0;
  std::uint64_t level_width =
      NextPowerOfTwo(static_cast<std::uint64_t>(leaf_count));
  while (level_width > 1U) {
    std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>> parents;
    for (std::size_t i = 0; i < current.size(); ++i) {
      const auto& [index, hash] = current[i];
      const bool has_adjacent =
          i + 1U < current.size() && current[i + 1U].first == (index ^ 1ULL);

      std::vector<std::uint8_t> left;
      std::vector<std::uint8_t> right;
      if (has_adjacent) {
        left = (index % 2ULL == 0ULL) ? hash : current[i + 1U].second;
        right = (index % 2ULL == 0ULL) ? current[i + 1U].second : hash;
        ++i;
      } else {
        if (sibling_cursor >= proof.sibling_hashes.size()) {
          return false;
        }
        const auto& sibling = proof.sibling_hashes[sibling_cursor++];
        left = (index % 2ULL == 0ULL) ? hash : sibling;
        right = (index % 2ULL == 0ULL) ? sibling : hash;
      }

      const auto parent_index = index / 2ULL;
      if (!parents.empty() && parents.back().first == parent_index) {
        return false;
      }
      parents.push_back({parent_index, HashParent(profile, left, right)});
    }
    current = std::move(parents);
    level_width /= 2ULL;
  }

  return sibling_cursor == proof.sibling_hashes.size() && current.size() == 1U &&
         current.front().second == root;
}

}  // namespace swgr::crypto
