#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "NTL/ZZ_pE.h"

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "tests/test_common.hpp"
#include "whir/common.hpp"
#include "whir/prover.hpp"
#include "whir/verifier.hpp"

int g_failures = 0;

namespace {

using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;

GRElem SmallElement(std::uint64_t value) {
  GRElem out;
  NTL::clear(out);
  GRElem one;
  NTL::set(one);
  for (std::uint64_t i = 0; i < value; ++i) {
    out += one;
  }
  return out;
}

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

swgr::whir::WhirPublicParameters BuildPublicParameters() {
  auto ctx =
      std::make_shared<GRContext>(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, 9);
  return ctx->with_ntl_context([&] {
    const GRElem omega = NTL::power(domain.root(), 3L);
    return swgr::whir::WhirPublicParameters{
        .ctx = ctx,
        .initial_domain = domain,
        .variable_count = 1,
        .layer_widths = {1},
        .shift_repetitions = {1},
        .final_repetitions = 1,
        .degree_bounds = {4},
        .deltas = {0.1L},
        .omega = omega,
        .ternary_grid = {ctx->one(), omega, omega * omega},
        .lambda_target = 128,
        .hash_profile = swgr::HashProfile::WHIR_NATIVE,
    };
  });
}

swgr::whir::MultiQuadraticPolynomial BuildPolynomial(const GRContext& ctx) {
  return ctx.with_ntl_context([&] {
    return swgr::whir::MultiQuadraticPolynomial(
        1, std::vector<GRElem>{SmallElement(2), SmallElement(3),
                               SmallElement(5)});
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

void TestCommitmentRootIsStableAndBindsTable() {
  testutil::PrintInfo("WHIR commit path produces stable table Merkle roots");

  const auto pp = BuildPublicParameters();
  const auto polynomial = BuildPolynomial(*pp.ctx);
  const swgr::whir::WhirProver prover(swgr::whir::WhirParameters{});

  swgr::whir::WhirCommitmentState lhs_state;
  const auto lhs = prover.commit(pp, polynomial, &lhs_state);
  swgr::whir::WhirCommitmentState rhs_state;
  const auto rhs = prover.commit(pp, polynomial, &rhs_state);

  CHECK_EQ(lhs.oracle_root, rhs.oracle_root);
  CHECK_EQ(lhs_state.oracle_root, lhs.oracle_root);
  CHECK_EQ(lhs_state.initial_oracle.size(),
           static_cast<std::size_t>(pp.initial_domain.size()));
  CHECK(lhs.stats.serialized_bytes > 0);

  auto tampered_oracle = lhs_state.initial_oracle;
  pp.ctx->with_ntl_context([&] {
    tampered_oracle.front() += pp.ctx->one();
    return 0;
  });
  const auto tampered_root =
      swgr::whir::build_oracle_tree(pp.hash_profile, *pp.ctx, tampered_oracle)
          .root();
  CHECK(tampered_root != lhs.oracle_root);
}

void TestCommitRejectsInvalidShapesAndUnsupportedPrime() {
  testutil::PrintInfo("WHIR commit path rejects wrong m and unsupported p");

  auto pp = BuildPublicParameters();
  const auto polynomial = BuildPolynomial(*pp.ctx);
  const swgr::whir::WhirProver prover(swgr::whir::WhirParameters{});

  bool wrong_m_threw = false;
  try {
    const auto wrong_m = pp.ctx->with_ntl_context([&] {
      return swgr::whir::MultiQuadraticPolynomial(
          2, std::vector<GRElem>{pp.ctx->one()});
    });
    swgr::whir::WhirCommitmentState state;
    (void)prover.commit(pp, wrong_m, &state);
  } catch (const std::invalid_argument&) {
    wrong_m_threw = true;
  }
  CHECK(wrong_m_threw);

  pp.ctx =
      std::make_shared<GRContext>(GRConfig{.p = 3, .k_exp = 2, .r = 2});
  bool bad_prime_threw = false;
  try {
    swgr::whir::WhirCommitmentState state;
    (void)prover.commit(pp, polynomial, &state);
  } catch (const std::invalid_argument&) {
    bad_prime_threw = true;
  }
  CHECK(bad_prime_threw);
}

void TestOpenProducesWellShapedProof() {
  testutil::PrintInfo("WHIR open path emits sumcheck, fold, and final openings");

  const auto pp = BuildPublicParameters();
  const auto polynomial = BuildPolynomial(*pp.ctx);
  const swgr::whir::WhirProver prover(swgr::whir::WhirParameters{});
  swgr::whir::WhirCommitmentState state;
  const auto commitment = prover.commit(pp, polynomial, &state);
  const auto point = pp.ctx->with_ntl_context(
      [&] { return std::vector<GRElem>{SmallElement(7)}; });

  const auto opening = prover.open(commitment, state, point);
  CHECK_EQ(opening.value, polynomial.evaluate(*pp.ctx, point));
  CHECK(swgr::whir::proof_shape_valid(opening.proof));
  CHECK_EQ(opening.proof.rounds.size(), std::size_t{1});
  CHECK_EQ(opening.proof.rounds[0].sumcheck_polynomials.size(),
           std::size_t{1});
  CHECK(!opening.proof.rounds[0].g_root.empty());
  CHECK(!opening.proof.rounds[0].virtual_fold_openings.queried_indices.empty());
  CHECK(!opening.proof.final_openings.queried_indices.empty());
  CHECK(opening.proof.stats.serialized_bytes > 0);
}

void TestOpenRejectsMismatchedState() {
  testutil::PrintInfo("WHIR open path rejects state/root mismatches");

  const auto pp = BuildPublicParameters();
  const auto polynomial = BuildPolynomial(*pp.ctx);
  const swgr::whir::WhirProver prover(swgr::whir::WhirParameters{});
  swgr::whir::WhirCommitmentState state;
  const auto commitment = prover.commit(pp, polynomial, &state);
  const auto point = pp.ctx->with_ntl_context(
      [&] { return std::vector<GRElem>{SmallElement(7)}; });

  state.oracle_root.front() ^= 0x01U;
  bool threw = false;
  try {
    (void)prover.open(commitment, state, point);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestVerifyAcceptsRoundtripOpening() {
  testutil::PrintInfo("WHIR verifier accepts an honest opening roundtrip");

  const auto pp = BuildPublicParameters();
  const auto polynomial = BuildPolynomial(*pp.ctx);
  const swgr::whir::WhirProver prover(swgr::whir::WhirParameters{});
  const swgr::whir::WhirVerifier verifier(swgr::whir::WhirParameters{});
  swgr::whir::WhirCommitmentState state;
  const auto commitment = prover.commit(pp, polynomial, &state);
  const auto point = pp.ctx->with_ntl_context(
      [&] { return std::vector<GRElem>{SmallElement(7)}; });
  const auto opening = prover.open(commitment, state, point);

  swgr::ProofStatistics stats;
  CHECK(verifier.verify(commitment, point, opening, &stats));
  CHECK(stats.serialized_bytes > 0);
}

void TestVerifyRejectsTamperingAndReplay() {
  testutil::PrintInfo("WHIR verifier rejects tampered proof messages");

  const auto pp = BuildPublicParameters();
  const auto polynomial = BuildPolynomial(*pp.ctx);
  const swgr::whir::WhirProver prover(swgr::whir::WhirParameters{});
  const swgr::whir::WhirVerifier verifier(swgr::whir::WhirParameters{});
  swgr::whir::WhirCommitmentState state;
  const auto commitment = prover.commit(pp, polynomial, &state);
  const auto point = pp.ctx->with_ntl_context(
      [&] { return std::vector<GRElem>{SmallElement(7)}; });
  const auto opening = prover.open(commitment, state, point);

  auto wrong_value = opening;
  pp.ctx->with_ntl_context([&] {
    wrong_value.value += pp.ctx->one();
    return 0;
  });
  CHECK(!verifier.verify(commitment, point, wrong_value));

  auto bad_sumcheck = opening;
  pp.ctx->with_ntl_context([&] {
    bad_sumcheck.proof.rounds[0].sumcheck_polynomials[0].coefficients[0] +=
        pp.ctx->one();
    return 0;
  });
  CHECK(!verifier.verify(commitment, point, bad_sumcheck));

  auto bad_root = opening;
  bad_root.proof.rounds[0].g_root[0] ^= 0x01U;
  CHECK(!verifier.verify(commitment, point, bad_root));

  auto bad_virtual_payload = opening;
  bad_virtual_payload.proof.rounds[0]
      .virtual_fold_openings.leaf_payloads[0][0] ^= 0x01U;
  CHECK(!verifier.verify(commitment, point, bad_virtual_payload));

  auto bad_final_constant = opening;
  pp.ctx->with_ntl_context([&] {
    bad_final_constant.proof.final_constant += pp.ctx->one();
    return 0;
  });
  CHECK(!verifier.verify(commitment, point, bad_final_constant));

  auto bad_final_payload = opening;
  bad_final_payload.proof.final_openings.leaf_payloads[0][0] ^= 0x01U;
  CHECK(!verifier.verify(commitment, point, bad_final_payload));

  const auto different_point = pp.ctx->with_ntl_context(
      [&] { return std::vector<GRElem>{SmallElement(8)}; });
  CHECK(!verifier.verify(commitment, different_point, opening));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestIndexedLabelsAreStableAndSeparated);
    RUN_TEST(TestSerializedBytesAreDeterministic);
    RUN_TEST(TestProofShapeValidation);
    RUN_TEST(TestCommitmentRootIsStableAndBindsTable);
    RUN_TEST(TestCommitRejectsInvalidShapesAndUnsupportedPrime);
    RUN_TEST(TestOpenProducesWellShapedProof);
    RUN_TEST(TestOpenRejectsMismatchedState);
    RUN_TEST(TestVerifyAcceptsRoundtripOpening);
    RUN_TEST(TestVerifyRejectsTamperingAndReplay);
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
