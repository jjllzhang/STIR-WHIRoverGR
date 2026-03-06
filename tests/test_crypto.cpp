#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "algebra/gr_context.hpp"
#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;

void TestTranscriptIsDeterministicAndDomainSeparated() {
  testutil::PrintInfo(
      "transcript stays deterministic and changes when absorbed bytes change");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  swgr::crypto::Transcript lhs(swgr::HashProfile::STIR_NATIVE);
  swgr::crypto::Transcript rhs(swgr::HashProfile::STIR_NATIVE);
  lhs.absorb_bytes(std::vector<std::uint8_t>{1, 2, 3, 4});
  rhs.absorb_bytes(std::vector<std::uint8_t>{1, 2, 3, 4});

  const auto lhs_ring = lhs.challenge_ring(ctx, "alpha");
  const auto rhs_ring = rhs.challenge_ring(ctx, "alpha");
  CHECK(lhs_ring == rhs_ring);
  CHECK_EQ(lhs.challenge_index("query", 17), rhs.challenge_index("query", 17));

  swgr::crypto::Transcript different(swgr::HashProfile::STIR_NATIVE);
  different.absorb_bytes(std::vector<std::uint8_t>{1, 2, 3, 9});
  CHECK(!(different.challenge_ring(ctx, "alpha") == lhs_ring));
}

void TestMerkleOpenVerifyAndRejectTamper() {
  testutil::PrintInfo(
      "merkle open/verify passes for deduped multi-query and rejects tamper");

  std::vector<std::vector<std::uint8_t>> leaves = {
      {0x10, 0x11}, {0x20, 0x21}, {0x30, 0x31}, {0x40, 0x41}};
  const swgr::crypto::MerkleTree tree(swgr::HashProfile::STIR_NATIVE, leaves);
  const auto proof = tree.open({2, 0, 2});

  CHECK_EQ(proof.queried_indices.size(), std::size_t{2});
  CHECK(swgr::crypto::MerkleTree::verify(swgr::HashProfile::STIR_NATIVE,
                                         leaves.size(), tree.root(), proof));

  auto tampered_payload = proof;
  tampered_payload.leaf_payloads[0][0] ^= 0x01U;
  CHECK(!swgr::crypto::MerkleTree::verify(swgr::HashProfile::STIR_NATIVE,
                                          leaves.size(), tree.root(),
                                          tampered_payload));

  auto tampered_sibling = proof;
  tampered_sibling.sibling_hashes[0][0] ^= 0x01U;
  CHECK(!swgr::crypto::MerkleTree::verify(swgr::HashProfile::STIR_NATIVE,
                                          leaves.size(), tree.root(),
                                          tampered_sibling));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestTranscriptIsDeterministicAndDomainSeparated);
    RUN_TEST(TestMerkleOpenVerifyAndRejectTamper);
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled std::exception: " << ex.what() << "\n";
    return 2;
  } catch (...) {
    std::cerr << "Unhandled non-std exception\n";
    return 2;
  }

  if (g_failures == 0) {
    std::cout << "\nAll tests passed.\n";
    return 0;
  }

  std::cerr << "\n" << g_failures << " test(s) failed.\n";
  return 1;
}
