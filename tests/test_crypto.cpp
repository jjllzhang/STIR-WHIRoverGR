#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "algebra/gr_context.hpp"
#include "algebra/teichmuller.hpp"
#include "crypto/hash.hpp"
#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "crypto/merkle_tree/proof_planner.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;

std::uint64_t NextPowerOfTwo(std::uint64_t value) {
  std::uint64_t power = 1;
  while (power < value) {
    power <<= 1U;
  }
  return power;
}

std::uint64_t CeilLog2(std::uint64_t value) {
  std::uint64_t result = 0;
  std::uint64_t current = 1;
  while (current < value) {
    current <<= 1U;
    ++result;
  }
  return result;
}

std::size_t UniqueCount(const std::vector<std::uint64_t>& values) {
  std::vector<std::uint64_t> unique = values;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  return unique.size();
}

void TestTranscriptIsDeterministicAndDomainSeparated() {
  testutil::PrintInfo(
      "transcript stays deterministic and changes when absorbed bytes change");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  stir_whir_gr::crypto::Transcript lhs(stir_whir_gr::HashProfile::STIR_NATIVE);
  stir_whir_gr::crypto::Transcript rhs(stir_whir_gr::HashProfile::STIR_NATIVE);
  lhs.absorb_bytes(std::vector<std::uint8_t>{1, 2, 3, 4});
  rhs.absorb_bytes(std::vector<std::uint8_t>{1, 2, 3, 4});

  const auto lhs_ring = lhs.challenge_ring(ctx, "alpha");
  const auto rhs_ring = rhs.challenge_ring(ctx, "alpha");
  CHECK(lhs_ring == rhs_ring);
  CHECK_EQ(lhs.challenge_index("query", 17), rhs.challenge_index("query", 17));

  stir_whir_gr::crypto::Transcript different(stir_whir_gr::HashProfile::STIR_NATIVE);
  different.absorb_bytes(std::vector<std::uint8_t>{1, 2, 3, 9});
  CHECK(!(different.challenge_ring(ctx, "alpha") == lhs_ring));
}

void TestTranscriptLabeledAbsorbSeparatesEqualPayloads() {
  testutil::PrintInfo(
      "transcript labeled absorbs domain-separate equal payload bytes");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const std::vector<std::uint8_t> payload{4, 3, 2, 1};

  stir_whir_gr::crypto::Transcript lhs(stir_whir_gr::HashProfile::STIR_NATIVE);
  stir_whir_gr::crypto::Transcript rhs(stir_whir_gr::HashProfile::STIR_NATIVE);
  lhs.absorb_labeled_bytes("label.a", payload);
  rhs.absorb_labeled_bytes("label.a", payload);
  CHECK(lhs.challenge_ring(ctx, "alpha") == rhs.challenge_ring(ctx, "alpha"));

  stir_whir_gr::crypto::Transcript different_label(stir_whir_gr::HashProfile::STIR_NATIVE);
  different_label.absorb_labeled_bytes("label.b", payload);
  stir_whir_gr::crypto::Transcript baseline(stir_whir_gr::HashProfile::STIR_NATIVE);
  baseline.absorb_labeled_bytes("label.a", payload);
  CHECK(!(different_label.challenge_ring(ctx, "alpha") ==
          baseline.challenge_ring(ctx, "alpha")));
}

void TestTranscriptTeichmullerChallengeIsDeterministicAndInT() {
  testutil::PrintInfo(
      "transcript teichmuller challenges stay deterministic and inside T");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  stir_whir_gr::crypto::Transcript lhs(stir_whir_gr::HashProfile::STIR_NATIVE);
  stir_whir_gr::crypto::Transcript rhs(stir_whir_gr::HashProfile::STIR_NATIVE);
  lhs.absorb_bytes(std::vector<std::uint8_t>{9, 8, 7, 6});
  rhs.absorb_bytes(std::vector<std::uint8_t>{9, 8, 7, 6});

  for (std::size_t round_index = 0; round_index < 16; ++round_index) {
    const auto label = std::string("beta:") + std::to_string(round_index);
    const auto lhs_beta = lhs.challenge_teichmuller(ctx, label);
    const auto rhs_beta = rhs.challenge_teichmuller(ctx, label);
    CHECK(lhs_beta == rhs_beta);
    CHECK(stir_whir_gr::algebra::is_teichmuller_element(ctx, lhs_beta));
  }

  stir_whir_gr::crypto::Transcript different(stir_whir_gr::HashProfile::STIR_NATIVE);
  different.absorb_bytes(std::vector<std::uint8_t>{9, 8, 7, 5});
  stir_whir_gr::crypto::Transcript baseline(stir_whir_gr::HashProfile::STIR_NATIVE);
  baseline.absorb_bytes(std::vector<std::uint8_t>{9, 8, 7, 6});
  CHECK(!(different.challenge_teichmuller(ctx, "beta:0") ==
          baseline.challenge_teichmuller(ctx, "beta:0")));
}

void TestHashBackendIsBlake3Only() {
  testutil::PrintInfo(
      "selected hash backend is blake3 and all hash paths use blake3");

  const std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8};
  const auto blake3 =
      stir_whir_gr::crypto::hash_bytes(stir_whir_gr::crypto::HashBackend::Blake3, payload);
  const auto blake3_again =
      stir_whir_gr::crypto::hash_bytes(stir_whir_gr::crypto::HashBackend::Blake3, payload);
  const auto different =
      stir_whir_gr::crypto::hash_bytes(stir_whir_gr::crypto::HashBackend::Blake3,
                               std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 9});

  CHECK_EQ(stir_whir_gr::crypto::selected_hash_backend(),
           stir_whir_gr::crypto::HashBackend::Blake3);
  CHECK_EQ(blake3.size(), std::size_t{32});
  CHECK_EQ(blake3, blake3_again);
  CHECK(blake3 != different);
}

void TestMerkleOpenVerifyAndRejectTamper() {
  testutil::PrintInfo(
      "merkle open/verify passes for deduped multi-query and rejects tamper");

  std::vector<std::vector<std::uint8_t>> leaves = {
      {0x10, 0x11}, {0x20, 0x21}, {0x30, 0x31}, {0x40, 0x41}};
  const stir_whir_gr::crypto::MerkleTree tree(stir_whir_gr::HashProfile::STIR_NATIVE, leaves);
  const auto proof = tree.open({2, 0, 2});

  CHECK_EQ(proof.queried_indices.size(), std::size_t{2});
  CHECK(stir_whir_gr::crypto::MerkleTree::verify(stir_whir_gr::HashProfile::STIR_NATIVE,
                                         leaves.size(), tree.root(), proof));

  auto tampered_payload = proof;
  tampered_payload.leaf_payloads[0][0] ^= 0x01U;
  CHECK(!stir_whir_gr::crypto::MerkleTree::verify(stir_whir_gr::HashProfile::STIR_NATIVE,
                                          leaves.size(), tree.root(),
                                          tampered_payload));

  auto tampered_sibling = proof;
  tampered_sibling.sibling_hashes[0][0] ^= 0x01U;
  CHECK(!stir_whir_gr::crypto::MerkleTree::verify(stir_whir_gr::HashProfile::STIR_NATIVE,
                                          leaves.size(), tree.root(),
                                          tampered_sibling));
}

void TestProofPlannerMatchesUniqueQueriesAndUpperBound() {
  testutil::PrintInfo(
      "proof planner counts deduped leaves and stays under non-pruned upper bound");

  const std::vector<std::uint64_t> queries = {4, 1, 4, 3, 1};
  const auto plan = stir_whir_gr::crypto::plan_pruned_multiproof(
      5, queries, /*leaf_payload_bytes=*/18, /*digest_bytes=*/32);
  const std::uint64_t unique_queries =
      static_cast<std::uint64_t>(UniqueCount(queries));
  const std::uint64_t padded_leaf_count = NextPowerOfTwo(5);
  const std::uint64_t non_pruned_upper_bound =
      unique_queries * CeilLog2(padded_leaf_count);

  CHECK_EQ(plan.opened_leaf_count, unique_queries);
  CHECK(plan.unique_sibling_count <= non_pruned_upper_bound);
}

void TestMerkleOpenVerifyOnNonPowerOfTwoLeafCount() {
  testutil::PrintInfo(
      "non-power-of-two merkle tree keeps pruned multi-openings verifiable");

  std::vector<std::vector<std::uint8_t>> leaves = {
      {0x10, 0x11}, {0x20, 0x21}, {0x30, 0x31}, {0x40, 0x41}, {0x50, 0x51}};
  const std::vector<std::uint64_t> queries = {4, 1, 4};
  const stir_whir_gr::crypto::MerkleTree tree(stir_whir_gr::HashProfile::STIR_NATIVE, leaves);
  const auto proof = tree.open(queries);
  const auto plan = stir_whir_gr::crypto::plan_pruned_multiproof(
      leaves.size(), queries, /*leaf_payload_bytes=*/2, /*digest_bytes=*/32);

  CHECK_EQ(proof.queried_indices.size(), UniqueCount(queries));
  CHECK_EQ(proof.leaf_payloads.size(),
           static_cast<std::size_t>(plan.opened_leaf_count));
  CHECK_EQ(proof.sibling_hashes.size(),
           static_cast<std::size_t>(plan.unique_sibling_count));
  CHECK(stir_whir_gr::crypto::MerkleTree::verify(stir_whir_gr::HashProfile::STIR_NATIVE,
                                         leaves.size(), tree.root(), proof));
}

void TestMerkleVerifyLargeMultiproofStillPasses() {
  testutil::PrintInfo(
      "large merkle multiproof stays verifiable across many parent reductions");

  constexpr std::size_t kLeafCount = 513;
  constexpr std::size_t kPayloadBytes = 48;
  std::vector<std::vector<std::uint8_t>> leaves(
      kLeafCount, std::vector<std::uint8_t>(kPayloadBytes));
  for (std::size_t leaf_index = 0; leaf_index < kLeafCount; ++leaf_index) {
    for (std::size_t byte_index = 0; byte_index < kPayloadBytes; ++byte_index) {
      leaves[leaf_index][byte_index] = static_cast<std::uint8_t>(
          (leaf_index * 17U + byte_index * 29U + 11U) & 0xFFU);
    }
  }

  std::vector<std::uint64_t> queries;
  queries.reserve(192);
  for (std::uint64_t i = 0; i < 192; ++i) {
    queries.push_back((i * 37U + (i % 5U)) %
                      static_cast<std::uint64_t>(kLeafCount));
  }

  const stir_whir_gr::crypto::MerkleTree tree(stir_whir_gr::HashProfile::STIR_NATIVE, leaves);
  const auto proof = tree.open(queries);
  CHECK(proof.queried_indices.size() >= 128U);
  CHECK(stir_whir_gr::crypto::MerkleTree::verify(stir_whir_gr::HashProfile::STIR_NATIVE,
                                         leaves.size(), tree.root(), proof));

  auto tampered = proof;
  tampered.sibling_hashes.back().back() ^= 0x5AU;
  CHECK(!stir_whir_gr::crypto::MerkleTree::verify(stir_whir_gr::HashProfile::STIR_NATIVE,
                                          leaves.size(), tree.root(), tampered));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestHashBackendIsBlake3Only);
    RUN_TEST(TestTranscriptIsDeterministicAndDomainSeparated);
    RUN_TEST(TestTranscriptLabeledAbsorbSeparatesEqualPayloads);
    RUN_TEST(TestTranscriptTeichmullerChallengeIsDeterministicAndInT);
    RUN_TEST(TestMerkleOpenVerifyAndRejectTamper);
    RUN_TEST(TestProofPlannerMatchesUniqueQueriesAndUpperBound);
    RUN_TEST(TestMerkleOpenVerifyOnNonPowerOfTwoLeafCount);
    RUN_TEST(TestMerkleVerifyLargeMultiproofStillPasses);
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
