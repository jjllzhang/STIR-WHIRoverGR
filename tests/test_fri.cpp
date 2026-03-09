#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "fri/common.hpp"
#include "fri/prover.hpp"
#include "fri/verifier.hpp"
#include "poly_utils/polynomial.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

using swgr::Domain;
using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;
using swgr::poly_utils::Polynomial;

std::uint64_t EncodedRingVectorBytes(const GRContext& ctx,
                                     std::size_t value_count) {
  return sizeof(std::uint64_t) +
         static_cast<std::uint64_t>(value_count) *
             (sizeof(std::uint64_t) +
              static_cast<std::uint64_t>(ctx.elem_bytes()));
}

std::uint64_t LegacyRawFriBytes(const GRContext& ctx,
                                const swgr::fri::FriProofWithWitness& artifact) {
  std::uint64_t bytes = swgr::fri::serialized_message_bytes(ctx, artifact.proof);
  bytes += sizeof(std::uint64_t);
  for (const auto& round_witness : artifact.witness.rounds) {
    bytes += EncodedRingVectorBytes(ctx, round_witness.oracle_evals.size());
  }
  return bytes;
}

Polynomial SamplePolynomial(const GRContext& ctx, const Domain& domain,
                            std::size_t coefficient_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(coefficient_count);

    GRElem root_power = ctx.one();
    for (std::size_t i = 0; i < coefficient_count; ++i) {
      coefficients.push_back(root_power + ctx.one());
      root_power *= domain.root();
    }
    coefficients.back() += ctx.one();
    return Polynomial(std::move(coefficients));
  });
}

swgr::fri::FriParameters MakeParams(
    std::uint64_t fold_factor,
    std::vector<std::uint64_t> query_repetitions = {2, 1},
    std::uint64_t stop_degree = 1) {
  swgr::fri::FriParameters params;
  params.fold_factor = fold_factor;
  params.stop_degree = stop_degree;
  params.query_repetitions = std::move(query_repetitions);
  params.lambda_target = 64;
  params.pow_bits = 0;
  params.sec_mode = swgr::SecurityMode::ConjectureCapacity;
  params.hash_profile = swgr::HashProfile::STIR_NATIVE;
  return params;
}

swgr::fri::FriParameters MakeAutoParams(std::uint64_t fold_factor,
                                        std::uint64_t stop_degree = 1) {
  return MakeParams(fold_factor, {}, stop_degree);
}

swgr::fri::FriInstance MakeInstance(const GRContext& ctx,
                                    std::uint64_t domain_size,
                                    std::uint64_t claimed_degree) {
  return swgr::fri::FriInstance{
      .domain = Domain::teichmuller_subgroup(ctx, domain_size),
      .claimed_degree = claimed_degree,
  };
}

void NudgePolynomial(const GRContext& ctx, Polynomial* polynomial) {
  ctx.with_ntl_context([&] {
    auto coefficients = polynomial->coefficients();
    coefficients[0] += ctx.one();
    *polynomial = Polynomial(std::move(coefficients));
    return 0;
  });
}

void TamperMerklePayload(swgr::crypto::MerkleProof* proof) {
  if (!proof->leaf_payloads.empty() && !proof->leaf_payloads.front().empty()) {
    proof->leaf_payloads.front().front() ^= 0x01U;
  } else if (!proof->sibling_hashes.empty() &&
             !proof->sibling_hashes.front().empty()) {
    proof->sibling_hashes.front().front() ^= 0x01U;
  }
}

void TamperOpeningValue(const GRContext& ctx, swgr::fri::FriOpening* opening) {
  ctx.with_ntl_context([&] {
    opening->claim.value += ctx.one();
    return 0;
  });
}

swgr::algebra::GRElem NonTeichAlpha(const GRContext& ctx) {
  return ctx.with_ntl_context([&] {
    auto candidate = ctx.one();
    candidate += ctx.one();
    candidate += ctx.one();
    return candidate;
  });
}

// Phase 3 baseline: public PCS verification now consumes only the external
// opening proof (committed sparse openings + quotient sparse proof).
void TestFriPcsCommitOpenVerifyRoundtrip() {
  testutil::PrintInfo(
      "fri pcs commit/open/verify accepts alpha in T \\ L with sparse openings only");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto alpha = ctx.zero();
  const auto opening = prover.open(commitment, polynomial, alpha);
  const auto compat = prover.open_with_witness(commitment, polynomial, alpha);

  CHECK(swgr::fri::commitment_domain_supported(commitment));
  CHECK(swgr::fri::opening_point_valid(commitment, alpha));
  CHECK_EQ(commitment.stats.serialized_bytes,
           swgr::fri::serialized_message_bytes(commitment));
  CHECK_EQ(opening.claim.value, polynomial.evaluate(ctx, alpha));
  CHECK(verifier.verify(commitment, opening.claim.alpha, opening.claim.value, opening));
  CHECK(verifier.verify(commitment, compat.opening.claim.alpha,
                        compat.opening.claim.value, compat));
  CHECK_EQ(opening.proof.stats.serialized_bytes,
           swgr::fri::serialized_message_bytes(ctx, opening));
  CHECK_EQ(opening.proof.committed_oracle_proof.queried_indices.size(),
           std::size_t{6});
  CHECK_EQ(opening.proof.quotient_proof.rounds.size(), std::size_t{2});
  CHECK_EQ(opening.proof.quotient_proof.rounds.back()
               .oracle_proof.queried_indices.size(),
           std::size_t{1});
}

void TestFriPcsRejectsAlphaInsideDomain() {
  testutil::PrintInfo("fri pcs rejects alpha that lies inside L");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto alpha = instance.domain.element(0);

  CHECK(!swgr::fri::opening_point_valid(commitment, alpha));
  bool threw = false;
  try {
    (void)prover.open(commitment, polynomial, alpha);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestFriPcsRejectsWrongPolynomialForCommitment() {
  testutil::PrintInfo(
      "fri pcs open rejects a polynomial that does not match the commitment");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto committed_polynomial = SamplePolynomial(ctx, instance.domain, 9);
  auto wrong_polynomial = committed_polynomial;
  NudgePolynomial(ctx, &wrong_polynomial);

  const swgr::fri::FriProver prover(params);
  const auto commitment = prover.commit(instance, committed_polynomial);

  bool threw = false;
  try {
    (void)prover.open(commitment, wrong_polynomial, ctx.zero());
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestFriPcsRejectsNonTeichAlpha() {
  testutil::PrintInfo("fri pcs rejects alpha outside the teichmuller set");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto alpha = NonTeichAlpha(ctx);

  CHECK(!swgr::fri::opening_point_valid(commitment, alpha));
  bool threw = false;
  try {
    (void)prover.open(commitment, polynomial, alpha);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestFriPcsRejectsNonTeichCommitmentDomain() {
  testutil::PrintInfo("fri pcs rejects commitments whose evaluation domain is not contained in T");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto params = MakeParams(3);
  const auto non_teich_offset = NonTeichAlpha(ctx);
  const swgr::fri::FriInstance bad_instance{
      .domain = Domain::teichmuller_coset(ctx, non_teich_offset, 9),
      .claimed_degree = 8,
  };
  const auto polynomial = SamplePolynomial(ctx, bad_instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  CHECK(!swgr::fri::commitment_domain_supported(
      swgr::fri::FriCommitment{.domain = bad_instance.domain, .degree_bound = 8}));
  bool threw = false;
  try {
    (void)prover.commit(bad_instance, polynomial);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestFriPcsRejectsTamperedClaimValue() {
  testutil::PrintInfo("fri pcs verifier rejects a tampered claimed value");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening = prover.open(commitment, polynomial, ctx.zero());

  TamperOpeningValue(ctx, &opening);

  CHECK(!verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriPcsRejectsTamperedCommittedOpening() {
  testutil::PrintInfo(
      "fri pcs verifier rejects a tampered sparse opening under the commitment root");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening = prover.open(commitment, polynomial, ctx.zero());

  TamperMerklePayload(&opening.proof.committed_oracle_proof);

  CHECK(!verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriPcsRejectsTamperedQuotientFinalPolynomial() {
  testutil::PrintInfo(
      "fri pcs verifier rejects a tampered quotient final polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening = prover.open(commitment, polynomial, ctx.zero());

  NudgePolynomial(ctx, &opening.proof.quotient_proof.final_polynomial);

  CHECK(!verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriPcsRejectsMismatchedAlpha() {
  testutil::PrintInfo(
      "fri pcs verifier rejects reusing an opening under a different alpha");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto opening = prover.open(commitment, polynomial, ctx.zero());
  const auto wrong_alpha = ctx.teich_generator();

  CHECK(swgr::fri::opening_point_valid(commitment, wrong_alpha));
  CHECK(!verifier.verify(commitment, wrong_alpha, opening.claim.value, opening));
}

void TestFriPcsRejectsMismatchedCommitment() {
  testutil::PrintInfo(
      "fri pcs verifier rejects reusing an opening under a different commitment");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);
  auto other_polynomial = polynomial;
  ctx.with_ntl_context([&] {
    auto coefficients = other_polynomial.coefficients();
    coefficients[1] += ctx.one();
    other_polynomial = Polynomial(std::move(coefficients));
    return 0;
  });

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto other_commitment = prover.commit(instance, other_polynomial);
  const auto opening = prover.open(commitment, polynomial, ctx.zero());

  CHECK(!verifier.verify(other_commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFri3HonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "fri-3 honest prover/verifier passes with sparse openings and final polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  const auto artifact = prover.prove_with_witness(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK(verifier.verify(instance, artifact));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{2});
  CHECK_EQ(proof.rounds.size(), std::size_t{3});
  CHECK_EQ(proof.oracle_roots.size(), std::size_t{3});
  CHECK_EQ(proof.rounds[0].oracle_proof.queried_indices.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[1].oracle_proof.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[2].oracle_proof.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.stats.serialized_bytes,
           swgr::fri::serialized_message_bytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawFriBytes(ctx, artifact));
}

void TestFri3RejectsTamperedRoundOpening() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered sparse round opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperMerklePayload(&proof.rounds[0].oracle_proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestFri3RejectsTamperedFinalPolynomial() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered final polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  NudgePolynomial(ctx, &proof.final_polynomial);

  CHECK(!verifier.verify(instance, proof));
}

void TestFri3RejectsTamperedTerminalOpening() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered terminal sparse opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperMerklePayload(&proof.rounds.back().oracle_proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestFri9HonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "fri-9 honest prover/verifier passes with one fold round plus terminal checks");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 8);
  const auto params = MakeParams(9, {2});
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  const auto artifact = prover.prove_with_witness(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{1});
  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  const auto round0_queries = proof.rounds[0].oracle_proof.queried_indices.size();
  CHECK(round0_queries >= std::size_t{1});
  CHECK(round0_queries <= std::size_t{2});
  CHECK_EQ(proof.rounds[1].oracle_proof.queried_indices.size(), round0_queries);
  CHECK_EQ(proof.stats.serialized_bytes,
           swgr::fri::serialized_message_bytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawFriBytes(ctx, artifact));
}

void TestFri9RejectsTamperedOpening() {
  testutil::PrintInfo("fri-9 verifier rejects a tampered sparse opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 8);
  const auto params = MakeParams(9, {2});
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperMerklePayload(&proof.rounds[0].oracle_proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestFriAutoScheduleMatchesConjectureCapacityDefault() {
  testutil::PrintInfo(
      "fri auto query schedule still drives the sparse proof round shape");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto auto_params = MakeAutoParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(auto_params);
  const swgr::fri::FriVerifier verifier(auto_params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.rounds.size(), std::size_t{3});
  CHECK_EQ(proof.rounds[0].oracle_proof.queried_indices.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[1].oracle_proof.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[2].oracle_proof.queried_indices.size(), std::size_t{1});
}

void TestFriManualQueriesOverrideAutoSchedule() {
  testutil::PrintInfo(
      "fri manual query schedule overrides conjecture-capacity auto scheduling");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const auto auto_params = MakeAutoParams(3);
  const auto manual_params = MakeParams(3, {1, 1});

  const swgr::fri::FriProver auto_prover(auto_params);
  const swgr::fri::FriVerifier auto_verifier(auto_params);
  const auto auto_proof = auto_prover.prove(instance, polynomial);

  const swgr::fri::FriProver manual_prover(manual_params);
  const swgr::fri::FriVerifier manual_verifier(manual_params);
  const auto manual_proof = manual_prover.prove(instance, polynomial);

  CHECK(auto_verifier.verify(instance, auto_proof));
  CHECK(manual_verifier.verify(instance, manual_proof));
  CHECK_EQ(auto_proof.rounds[0].oracle_proof.queried_indices.size(), std::size_t{2});
  CHECK_EQ(auto_proof.rounds[1].oracle_proof.queried_indices.size(), std::size_t{1});
  CHECK_EQ(manual_proof.rounds[0].oracle_proof.queried_indices.size(),
           std::size_t{1});
  CHECK_EQ(manual_proof.rounds[1].oracle_proof.queried_indices.size(),
           std::size_t{1});
}

void TestFriQueriesAreCappedToBundleCount() {
  testutil::PrintInfo(
      "fri manual over-budget queries are capped to the bundle count");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 8);
  const auto params = MakeParams(9, {10});
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  CHECK(verifier.verify(instance, proof));

  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  const auto round0_queries = proof.rounds[0].oracle_proof.queried_indices.size();
  CHECK(round0_queries >= std::size_t{1});
  CHECK(round0_queries <= std::size_t{3});
  CHECK_EQ(proof.rounds[1].oracle_proof.queried_indices.size(), round0_queries);
}

void TestBuildOracleLeavesMatchesBundleSerialization() {
  testutil::PrintInfo(
      "fri oracle leaf builder matches per-bundle serialization at parallel-sized bundle counts");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  constexpr std::uint64_t kBundleSize = 3;
  constexpr std::uint64_t kBundleCount = 128;
  const auto oracle_evals = ctx.with_ntl_context([&] {
    std::vector<GRElem> values;
    values.reserve(static_cast<std::size_t>(kBundleSize * kBundleCount));

    GRElem current = ctx.one();
    GRElem delta = ctx.one() + ctx.teich_generator();
    for (std::uint64_t i = 0; i < kBundleSize * kBundleCount; ++i) {
      values.push_back(current);
      current += delta;
    }
    return values;
  });

  const auto leaves =
      swgr::fri::build_oracle_leaves(ctx, oracle_evals, kBundleSize);
  CHECK_EQ(leaves.size(), static_cast<std::size_t>(kBundleCount));

  for (std::uint64_t bundle_index = 0; bundle_index < kBundleCount;
       ++bundle_index) {
    const auto serialized = swgr::fri::serialize_oracle_bundle(
        ctx, oracle_evals, kBundleSize, bundle_index);
    CHECK(leaves[static_cast<std::size_t>(bundle_index)] == serialized);

    const auto decoded = swgr::fri::deserialize_oracle_bundle(
        ctx, leaves[static_cast<std::size_t>(bundle_index)]);
    CHECK_EQ(decoded.size(), static_cast<std::size_t>(kBundleSize));
    for (std::uint64_t offset = 0; offset < kBundleSize; ++offset) {
      const std::uint64_t oracle_index = bundle_index + offset * kBundleCount;
      CHECK(ctx.serialize(decoded[static_cast<std::size_t>(offset)]) ==
            ctx.serialize(oracle_evals[static_cast<std::size_t>(oracle_index)]));
    }
  }
}

void TestFriValidationRejectsBadInputs() {
  testutil::PrintInfo("fri parameter validation rejects zero queries and bad degree");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  auto params = MakeParams(3);
  params.query_repetitions = {2, 0};
  CHECK(!swgr::fri::validate(params));

  const auto instance = MakeInstance(ctx, 9, 8);
  CHECK(swgr::fri::validate(MakeParams(3), instance));

  const swgr::fri::FriInstance bad_instance{
      .domain = instance.domain,
      .claimed_degree = instance.domain.size(),
  };
  CHECK(!swgr::fri::validate(MakeParams(3), bad_instance));

  const GRContext fri9_ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  CHECK(swgr::fri::validate(MakeParams(9, {2}), MakeInstance(fri9_ctx, 27, 8)));
  CHECK(!swgr::fri::validate(MakeParams(9, {2}), MakeInstance(fri9_ctx, 27, 26)));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestFriPcsCommitOpenVerifyRoundtrip);
    RUN_TEST(TestFriPcsRejectsAlphaInsideDomain);
    RUN_TEST(TestFriPcsRejectsWrongPolynomialForCommitment);
    RUN_TEST(TestFriPcsRejectsNonTeichAlpha);
    RUN_TEST(TestFriPcsRejectsNonTeichCommitmentDomain);
    RUN_TEST(TestFriPcsRejectsTamperedClaimValue);
    RUN_TEST(TestFriPcsRejectsTamperedCommittedOpening);
    RUN_TEST(TestFriPcsRejectsTamperedQuotientFinalPolynomial);
    RUN_TEST(TestFriPcsRejectsMismatchedAlpha);
    RUN_TEST(TestFriPcsRejectsMismatchedCommitment);
    RUN_TEST(TestFri3HonestRoundtripAndRoundShape);
    RUN_TEST(TestFri3RejectsTamperedRoundOpening);
    RUN_TEST(TestFri3RejectsTamperedFinalPolynomial);
    RUN_TEST(TestFri3RejectsTamperedTerminalOpening);
    RUN_TEST(TestFri9HonestRoundtripAndRoundShape);
    RUN_TEST(TestFri9RejectsTamperedOpening);
    RUN_TEST(TestFriAutoScheduleMatchesConjectureCapacityDefault);
    RUN_TEST(TestFriManualQueriesOverrideAutoSchedule);
    RUN_TEST(TestFriQueriesAreCappedToBundleCount);
    RUN_TEST(TestBuildOracleLeavesMatchesBundleSerialization);
    RUN_TEST(TestFriValidationRejectsBadInputs);
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled std::exception: " << ex.what() << "\n";
    return 2;
  } catch (...) {
    std::cerr << "Unhandled non-std exception\n";
    return 2;
  }

  if (g_failures != 0) {
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
  }
  return 0;
}
