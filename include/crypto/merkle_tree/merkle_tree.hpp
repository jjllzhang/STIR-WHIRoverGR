#ifndef STIR_WHIR_GR_CRYPTO_MERKLE_TREE_MERKLE_TREE_HPP_
#define STIR_WHIR_GR_CRYPTO_MERKLE_TREE_MERKLE_TREE_HPP_

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "parameters.hpp"

namespace stir_whir_gr::crypto {

struct MerkleProof {
  std::vector<std::uint64_t> queried_indices;
  std::vector<std::vector<std::uint8_t>> leaf_payloads;
  std::vector<std::vector<std::uint8_t>> sibling_hashes;
};

class MerkleTree {
 public:
  explicit MerkleTree(
      std::vector<std::vector<std::uint8_t>> leaves,
      HashProfile profile = HashProfile::STIR_NATIVE);
  MerkleTree(HashProfile profile, std::vector<std::vector<std::uint8_t>> leaves)
      : MerkleTree(std::move(leaves), profile) {}

  std::size_t leaf_count() const { return leaves_.size(); }
  const std::vector<std::uint8_t>& root() const { return root_; }

  MerkleProof open(const std::vector<std::uint64_t>& queried_indices) const;

  static bool verify(const std::vector<std::uint8_t>& root,
                     std::size_t leaf_count, const MerkleProof& proof,
                     HashProfile profile = HashProfile::STIR_NATIVE);
  static bool verify(HashProfile profile, std::size_t leaf_count,
                     const std::vector<std::uint8_t>& root,
                     const MerkleProof& proof) {
    return verify(root, leaf_count, proof, profile);
  }

 private:
  std::vector<std::vector<std::vector<std::uint8_t>>> levels_;
  HashProfile profile_;
  std::vector<std::vector<std::uint8_t>> leaves_;
  std::vector<std::uint8_t> root_;
};

}  // namespace stir_whir_gr::crypto

#endif  // STIR_WHIR_GR_CRYPTO_MERKLE_TREE_MERKLE_TREE_HPP_
