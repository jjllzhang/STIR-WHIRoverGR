#include "crypto/merkle_tree/merkle_tree.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::crypto {

MerkleTree::MerkleTree(std::vector<std::vector<std::uint8_t>> leaves)
    : leaves_(std::move(leaves)) {
  if (!leaves_.empty()) {
    root_ = leaves_.front();
  }
}

MerkleProof MerkleTree::open(
    const std::vector<std::uint64_t>& queried_indices) const {
  (void)queried_indices;
  throw_unimplemented("crypto::MerkleTree::open");
}

}  // namespace swgr::crypto
