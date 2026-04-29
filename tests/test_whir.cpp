#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "algebra/gr_context.hpp"
#include "tests/test_common.hpp"
#include "whir/common.hpp"

int g_failures = 0;

namespace {

using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;

swgr::crypto::MerkleProof FakeMerkleProof() {
  swgr::crypto::MerkleProof proof;
  proof.queried_indices = {3};
  proof.leaf_payloads = {{0x10, 0x20, 0x30}};
  proof.sibling_hashes = {{0x01, 0x02, 0x03, 0x04}};
  return proof;
}

swgr::whir::WhirProof BuildWellShapedProof(const GRContext& ctx) {
  return ctx.with_ntl_context([&] {
    swgr::whir::WhirProof proof;
    proof.rounds.push_back(swgr::whir::WhirRoundProof{
        .sumcheck_polynomials =
            {swgr::whir::WhirSumcheckPolynomial{
                 .coefficients = {ctx.one(), ctx.zero()}},
             swgr::whir::WhirSumcheckPolynomial{
                 .coefficients = {ctx.one() + ctx.one()}}},
        .g_root = {0xa0, 0xa1, 0xa2},
        .virtual_fold_openings = FakeMerkleProof(),
    });
    proof.final_constant = ctx.one();
    proof.final_openings = FakeMerkleProof();
    return proof;
  });
}

void TestIndexedLabelsAreStableAndSeparated() {
  testutil::PrintInfo("WHIR transcript labels are stable and round-indexed");

  CHECK_EQ(swgr::whir::indexed_label(swgr::whir::kTranscriptLabelAlpha, 2),
           std::string("whir.alpha:2"));
  CHECK_EQ(swgr::whir::indexed_label(
               swgr::whir::kTranscriptLabelSumcheckPolynomial, 2, 1),
           std::string("whir.sumcheck_polynomial:2:1"));
  CHECK(swgr::whir::indexed_label(swgr::whir::kTranscriptLabelGamma, 0) !=
        swgr::whir::indexed_label(swgr::whir::kTranscriptLabelShift, 0));
  CHECK_EQ(std::string(swgr::whir::kTranscriptLabelFinalQuery),
           std::string("whir.final_query"));
}

void TestSerializedBytesAreDeterministic() {
  testutil::PrintInfo(
      "WHIR proof and opening byte accounting is deterministic");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto proof = BuildWellShapedProof(ctx);
  const auto lhs = swgr::whir::serialized_message_bytes(ctx, proof);
  const auto rhs = swgr::whir::serialized_message_bytes(ctx, proof);
  const auto opening = ctx.with_ntl_context([&] {
    return swgr::whir::WhirOpening{.value = ctx.one() + ctx.one(),
                                   .proof = proof};
  });
  const auto opening_bytes =
      swgr::whir::serialized_message_bytes(ctx, opening);

  CHECK(lhs > 0);
  CHECK_EQ(lhs, rhs);
  CHECK(opening_bytes > lhs);
}

void TestProofShapeValidation() {
  testutil::PrintInfo("WHIR proof shape rejects incomplete proof envelopes");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  swgr::whir::WhirProof empty;
  CHECK(!swgr::whir::proof_shape_valid(empty));

  auto missing_root = BuildWellShapedProof(ctx);
  missing_root.rounds[0].g_root.clear();
  CHECK(!swgr::whir::proof_shape_valid(missing_root));

  auto mismatched_merkle_payloads = BuildWellShapedProof(ctx);
  mismatched_merkle_payloads.final_openings.leaf_payloads.clear();
  CHECK(!swgr::whir::proof_shape_valid(mismatched_merkle_payloads));

  CHECK(swgr::whir::proof_shape_valid(BuildWellShapedProof(ctx)));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestIndexedLabelsAreStableAndSeparated);
    RUN_TEST(TestSerializedBytesAreDeterministic);
    RUN_TEST(TestProofShapeValidation);
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
