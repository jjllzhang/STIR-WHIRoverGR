#ifndef SWGR_CRYPTO_MERKLE_TREE_MERKLE_TREE_HPP_
#define SWGR_CRYPTO_MERKLE_TREE_MERKLE_TREE_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace swgr::crypto {

struct MerkleProof {
  std::vector<std::uint64_t> queried_indices;
  std::vector<std::vector<std::uint8_t>> sibling_hashes;
};

class MerkleTree {
 public:
  explicit MerkleTree(std::vector<std::vector<std::uint8_t>> leaves);

  std::size_t leaf_count() const { return leaves_.size(); }
  const std::vector<std::uint8_t>& root() const { return root_; }

  MerkleProof open(const std::vector<std::uint64_t>& queried_indices) const;

 private:
  std::vector<std::vector<std::uint8_t>> leaves_;
  std::vector<std::uint8_t> root_;
};

}  // namespace swgr::crypto

#endif  // SWGR_CRYPTO_MERKLE_TREE_MERKLE_TREE_HPP_
