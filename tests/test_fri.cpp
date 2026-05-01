#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "algebra/gr_context.hpp"
#include "algebra/teichmuller.hpp"
#include "crypto/fs/transcript.hpp"
#include "domain.hpp"
#include "fri/common.hpp"
#include "fri/parameters.hpp"
#include "fri/prover.hpp"
#include "fri/soundness.hpp"
#include "fri/verifier.hpp"
#include "poly_utils/polynomial.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

using stir_whir_gr::Domain;
using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;
using stir_whir_gr::algebra::GRElem;
using stir_whir_gr::poly_utils::Polynomial;

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

stir_whir_gr::fri::FriParameters MakeParams(
    std::uint64_t fold_factor,
    std::uint64_t repetition_count = 2,
    std::uint64_t stop_degree = 1) {
  stir_whir_gr::fri::FriParameters params;
  params.fold_factor = fold_factor;
  params.stop_degree = stop_degree;
  params.repetition_count = repetition_count;
  params.hash_profile = stir_whir_gr::HashProfile::STIR_NATIVE;
  return params;
}

stir_whir_gr::fri::FriInstance MakeInstance(const GRContext& ctx,
                                    std::uint64_t domain_size,
                                    std::uint64_t claimed_degree) {
  return stir_whir_gr::fri::FriInstance{
      .domain = Domain::teichmuller_subgroup(ctx, domain_size),
      .claimed_degree = claimed_degree,
  };
}

void TamperMerklePayload(stir_whir_gr::crypto::MerkleProof* proof) {
  if (!proof->leaf_payloads.empty() && !proof->leaf_payloads.front().empty()) {
    proof->leaf_payloads.front().front() ^= 0x01U;
  } else if (!proof->sibling_hashes.empty() &&
             !proof->sibling_hashes.front().empty()) {
    proof->sibling_hashes.front().front() ^= 0x01U;
  }
}

void TamperOpeningValue(const GRContext& ctx, stir_whir_gr::fri::FriOpening* opening) {
  ctx.with_ntl_context([&] {
    opening->claim.value += ctx.one();
    return 0;
  });
}

void TamperFinalOracle(const GRContext& ctx, stir_whir_gr::fri::FriOpening* opening) {
  ctx.with_ntl_context([&] {
    if (opening->proof.final_oracle.empty()) {
      return 0;
    }
    opening->proof.final_oracle.front() += ctx.one();
    return 0;
  });
}

stir_whir_gr::algebra::GRElem NonTeichAlpha(const GRContext& ctx) {
  return ctx.with_ntl_context([&] {
    auto candidate = ctx.one();
    candidate += ctx.one();
    candidate += ctx.one();
    return candidate;
  });
}

std::string FoldRoundLabel(std::size_t round_index) {
  return "fri.fold_alpha:" + std::to_string(round_index);
}

std::vector<std::uint8_t> RoundRootBytes(std::size_t round_index) {
  return {
      static_cast<std::uint8_t>(0xA0U + (round_index & 0x0FU)),
      static_cast<std::uint8_t>((round_index * 17U + 3U) & 0xFFU),
      static_cast<std::uint8_t>((round_index * 29U + 5U) & 0xFFU),
      static_cast<std::uint8_t>((round_index * 43U + 7U) & 0xFFU),
  };
}

void TestFriFoldChallengesAlwaysLieInTeichmullerSet() {
  testutil::PrintInfo("fri folding challenges are always sampled from T");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  stir_whir_gr::crypto::Transcript transcript(stir_whir_gr::HashProfile::STIR_NATIVE);

  for (std::size_t round_index = 0; round_index < 16; ++round_index) {
    transcript.absorb_bytes(RoundRootBytes(round_index));
    const auto beta = stir_whir_gr::fri::derive_fri_folding_challenge(
        transcript, ctx, FoldRoundLabel(round_index));
    CHECK(stir_whir_gr::algebra::is_teichmuller_element(ctx, beta));
  }
}

void TestFriFoldChallengeReplayMatchesAcrossTranscripts() {
  testutil::PrintInfo(
      "fri prover and verifier replay the same teichmuller folding challenges");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  stir_whir_gr::crypto::Transcript prover(stir_whir_gr::HashProfile::STIR_NATIVE);
  stir_whir_gr::crypto::Transcript verifier(stir_whir_gr::HashProfile::STIR_NATIVE);

  for (std::size_t round_index = 0; round_index < 16; ++round_index) {
    const auto root_bytes = RoundRootBytes(round_index);
    prover.absorb_bytes(root_bytes);
    verifier.absorb_bytes(root_bytes);
    const auto prover_beta = stir_whir_gr::fri::derive_fri_folding_challenge(
        prover, ctx, FoldRoundLabel(round_index));
    const auto verifier_beta = stir_whir_gr::fri::derive_fri_folding_challenge(
        verifier, ctx, FoldRoundLabel(round_index));
    CHECK(prover_beta == verifier_beta);
  }
}

void TestFriFoldChallengesDoNotReuseGenericRingSampling() {
  testutil::PrintInfo(
      "fri folding challenges no longer reuse unrestricted ring sampling");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  bool found_non_teich_generic = false;

  for (std::size_t attempt = 0; attempt < 16 && !found_non_teich_generic;
       ++attempt) {
    stir_whir_gr::crypto::Transcript generic(stir_whir_gr::HashProfile::STIR_NATIVE);
    stir_whir_gr::crypto::Transcript theorem(stir_whir_gr::HashProfile::STIR_NATIVE);
    const auto root_bytes = RoundRootBytes(attempt);
    generic.absorb_bytes(root_bytes);
    theorem.absorb_bytes(root_bytes);

    const auto generic_beta = generic.challenge_ring(ctx, FoldRoundLabel(0));
    const auto theorem_beta = stir_whir_gr::fri::derive_fri_folding_challenge(
        theorem, ctx, FoldRoundLabel(0));
    if (!stir_whir_gr::algebra::is_teichmuller_element(ctx, generic_beta)) {
      found_non_teich_generic = true;
      CHECK(stir_whir_gr::algebra::is_teichmuller_element(ctx, theorem_beta));
      CHECK(!(generic_beta == theorem_beta));
    }
  }

  CHECK(found_non_teich_generic);
}

void TestFriRepetitionCountMapsToFreshRoundQueries() {
  testutil::PrintInfo(
      "fri repetition count m maps to fresh per-round query sampling");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3, 2);

  const auto metadata = stir_whir_gr::fri::resolve_query_rounds_metadata(params, instance);
  CHECK_EQ(metadata.size(), std::size_t{2});
  CHECK_EQ(metadata[0].query_chain_count, std::uint64_t{2});
  CHECK_EQ(metadata[0].fresh_query_count, std::uint64_t{2});
  CHECK_EQ(metadata[0].bundle_count, std::uint64_t{3});
  CHECK(!metadata[0].carries_previous_queries);
  CHECK_EQ(metadata[1].query_chain_count, std::uint64_t{2});
  CHECK_EQ(metadata[1].fresh_query_count, std::uint64_t{2});
  CHECK_EQ(metadata[1].bundle_count, std::uint64_t{1});
  CHECK(!metadata[1].carries_previous_queries);
  CHECK_EQ(stir_whir_gr::fri::terminal_query_chain_count(params), std::uint64_t{2});
}

void TestStandaloneFriSoundnessSolvesMainPreset() {
  testutil::PrintInfo(
      "standalone FRI PCS theorem auto solver matches the main preset m");

  const auto analysis = stir_whir_gr::fri::analyze_standalone_soundness(
      stir_whir_gr::fri::StandaloneFriSoundnessInputs{
          .base_prime = 2,
          .ring_extension_degree = 162,
          .domain_size = 243,
          .fold_factor = 3,
          .quotient_code_dimension = 81,
          .lambda_target = 128,
      });

  CHECK(analysis.span_term_within_target);
  CHECK_EQ(analysis.delta_numerator, std::uint64_t{1});
  CHECK_EQ(analysis.delta_denominator, std::uint64_t{3});
  CHECK_EQ(analysis.minimum_repetition_count, std::uint64_t{219});
}

void TestStandaloneFriSoundnessRejectsImpossibleSpanTerm() {
  testutil::PrintInfo(
      "standalone FRI PCS theorem auto solver detects impossible span terms");

  const auto analysis = stir_whir_gr::fri::analyze_standalone_soundness(
      stir_whir_gr::fri::StandaloneFriSoundnessInputs{
          .base_prime = 2,
          .ring_extension_degree = 32,
          .domain_size = 243,
          .fold_factor = 9,
          .quotient_code_dimension = 81,
          .lambda_target = 64,
      });

  CHECK(!analysis.span_term_within_target);
  CHECK_EQ(analysis.minimum_repetition_count, std::uint64_t{110});
}

void TestStandaloneFriSoundnessRejectsZeroDelta() {
  testutil::PrintInfo(
      "standalone FRI PCS theorem auto solver rejects delta=0 instances");

  bool threw = false;
  try {
    (void)stir_whir_gr::fri::analyze_standalone_soundness(
        stir_whir_gr::fri::StandaloneFriSoundnessInputs{
            .base_prime = 2,
            .ring_extension_degree = 54,
            .domain_size = 9,
            .fold_factor = 3,
            .quotient_code_dimension = 8,
            .lambda_target = 64,
        });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestFriPcsCommitOpenVerifyRoundtripAlphaOutsideDomain() {
  testutil::PrintInfo(
      "fri pcs commit/open/verify accepts alpha in T outside the evaluation domain");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3, 2);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto alpha = ctx.zero();
  const auto opening = prover.open(commitment, polynomial, alpha);

  CHECK(stir_whir_gr::fri::commitment_domain_supported(commitment));
  CHECK(stir_whir_gr::fri::opening_point_valid(commitment, alpha));
  CHECK_EQ(commitment.stats.serialized_bytes,
           stir_whir_gr::fri::serialized_message_bytes(commitment));
  CHECK_EQ(opening.claim.value, polynomial.evaluate(ctx, alpha));
  CHECK(verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                        opening));
  CHECK_EQ(opening.proof.stats.serialized_bytes,
           stir_whir_gr::fri::serialized_message_bytes(ctx, opening));
  CHECK_EQ(opening.proof.oracle_roots.size(), std::size_t{2});
  CHECK_EQ(opening.proof.rounds.size(), std::size_t{2});
  CHECK_EQ(opening.proof.final_oracle.size(), std::size_t{1});
}

void TestFriPcsCommitOpenVerifyRoundtripAlphaInsideDomain() {
  testutil::PrintInfo(
      "fri pcs commit/open/verify accepts alpha that lies inside the evaluation domain");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3, 2);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto alpha = instance.domain.element(0);
  const auto opening = prover.open(commitment, polynomial, alpha);

  CHECK(stir_whir_gr::fri::opening_point_valid(commitment, alpha));
  CHECK_EQ(opening.claim.value, polynomial.evaluate(ctx, alpha));
  CHECK(verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                        opening));
}

void TestFriZeroFoldRoundtrip() {
  testutil::PrintInfo(
      "zero-fold theorem-facing fri reveals only the committed table and keeps g0 virtual");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 1);
  const auto params = MakeParams(3, 3, 1);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 2);

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto alpha = instance.domain.element(0);
  const auto opening = prover.open(commitment, polynomial, alpha);

  CHECK(verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                        opening));
  CHECK(opening.proof.oracle_roots.empty());
  CHECK(opening.proof.rounds.empty());
  CHECK_EQ(opening.proof.revealed_committed_oracle.size(), std::size_t{9});
  CHECK(opening.proof.final_oracle.empty());
}

void TestFriPcsRejectsWrongPolynomialForCommitment() {
  testutil::PrintInfo(
      "fri pcs open rejects a polynomial that does not match the commitment");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto committed_polynomial = SamplePolynomial(ctx, instance.domain, 9);
  auto wrong_polynomial = committed_polynomial;
  ctx.with_ntl_context([&] {
    auto coefficients = wrong_polynomial.coefficients();
    coefficients[0] += ctx.one();
    wrong_polynomial = Polynomial(std::move(coefficients));
    return 0;
  });

  const stir_whir_gr::fri::FriProver prover(params);
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

  const stir_whir_gr::fri::FriProver prover(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto alpha = NonTeichAlpha(ctx);

  CHECK(!stir_whir_gr::fri::opening_point_valid(commitment, alpha));
  bool threw = false;
  try {
    (void)prover.open(commitment, polynomial, alpha);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestFriPcsRejectsNonTeichCommitmentDomain() {
  testutil::PrintInfo(
      "fri pcs rejects commitments whose evaluation domain is not contained in T");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto params = MakeParams(3);
  const auto non_teich_offset = NonTeichAlpha(ctx);
  const stir_whir_gr::fri::FriInstance bad_instance{
      .domain = Domain::teichmuller_coset(ctx, non_teich_offset, 9),
      .claimed_degree = 8,
  };
  const auto polynomial = SamplePolynomial(ctx, bad_instance.domain, 9);

  const stir_whir_gr::fri::FriProver prover(params);
  CHECK(!stir_whir_gr::fri::commitment_domain_supported(
      stir_whir_gr::fri::FriCommitment{.domain = bad_instance.domain, .degree_bound = 8}));
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

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening = prover.open(commitment, polynomial, ctx.zero());

  TamperOpeningValue(ctx, &opening);

  CHECK(!verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriPcsRejectsTamperedFirstRoundParentOpening() {
  testutil::PrintInfo(
      "fri pcs verifier rejects a tampered first-round parent opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening = prover.open(commitment, polynomial, ctx.zero());

  TamperMerklePayload(&opening.proof.rounds.front().parent_oracle_proof);

  CHECK(!verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriPcsRejectsTamperedIntermediateChildOpening() {
  testutil::PrintInfo(
      "fri pcs verifier rejects a tampered intermediate child opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening = prover.open(commitment, polynomial, ctx.zero());

  TamperMerklePayload(&opening.proof.rounds.front().child_oracle_proof);

  CHECK(!verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriPcsRejectsTamperedAlphaInsideDomainFirstRoundChildOpening() {
  testutil::PrintInfo(
      "fri pcs verifier rejects a tampered first-round child opening when alpha lies inside the evaluation domain");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening =
      prover.open(commitment, polynomial, instance.domain.element(0));

  TamperMerklePayload(&opening.proof.rounds.front().child_oracle_proof);

  CHECK(!verifier.verify(commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriPcsRejectsTamperedFinalOracleTable() {
  testutil::PrintInfo(
      "fri pcs verifier rejects a tampered final oracle table");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  auto opening = prover.open(commitment, polynomial, ctx.zero());

  TamperFinalOracle(ctx, &opening);

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

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto opening = prover.open(commitment, polynomial, ctx.zero());
  const auto wrong_alpha = ctx.teich_generator();

  CHECK(stir_whir_gr::fri::opening_point_valid(commitment, wrong_alpha));
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

  const stir_whir_gr::fri::FriProver prover(params);
  const stir_whir_gr::fri::FriVerifier verifier(params);
  const auto commitment = prover.commit(instance, polynomial);
  const auto other_commitment = prover.commit(instance, other_polynomial);
  const auto opening = prover.open(commitment, polynomial, ctx.zero());

  CHECK(!verifier.verify(other_commitment, opening.claim.alpha, opening.claim.value,
                         opening));
}

void TestFriValidationRejectsBadInputs() {
  testutil::PrintInfo(
      "fri parameter validation rejects zero repetition counts and bad degree");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  auto params = MakeParams(3);
  params.repetition_count = 0;
  CHECK(!stir_whir_gr::fri::validate(params));

  const auto instance = MakeInstance(ctx, 9, 8);
  CHECK(stir_whir_gr::fri::validate(MakeParams(3), instance));

  const stir_whir_gr::fri::FriInstance bad_degree{
      .domain = instance.domain,
      .claimed_degree = 9,
  };
  CHECK(!stir_whir_gr::fri::validate(MakeParams(3), bad_degree));
}

}  // namespace

int main() {
  try {
    TestFriFoldChallengesAlwaysLieInTeichmullerSet();
    TestFriFoldChallengeReplayMatchesAcrossTranscripts();
    TestFriFoldChallengesDoNotReuseGenericRingSampling();
    TestFriRepetitionCountMapsToFreshRoundQueries();
    TestStandaloneFriSoundnessSolvesMainPreset();
    TestStandaloneFriSoundnessRejectsImpossibleSpanTerm();
    TestStandaloneFriSoundnessRejectsZeroDelta();
    TestFriPcsCommitOpenVerifyRoundtripAlphaOutsideDomain();
    TestFriPcsCommitOpenVerifyRoundtripAlphaInsideDomain();
    TestFriZeroFoldRoundtrip();
    TestFriPcsRejectsWrongPolynomialForCommitment();
    TestFriPcsRejectsNonTeichAlpha();
    TestFriPcsRejectsNonTeichCommitmentDomain();
    TestFriPcsRejectsTamperedClaimValue();
    TestFriPcsRejectsTamperedFirstRoundParentOpening();
    TestFriPcsRejectsTamperedIntermediateChildOpening();
    TestFriPcsRejectsTamperedAlphaInsideDomainFirstRoundChildOpening();
    TestFriPcsRejectsTamperedFinalOracleTable();
    TestFriPcsRejectsMismatchedAlpha();
    TestFriPcsRejectsMismatchedCommitment();
    TestFriValidationRejectsBadInputs();
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled exception: " << ex.what() << '\n';
    return 1;
  }

  if (g_failures != 0) {
    std::cerr << g_failures << " checks failed\n";
    return 1;
  }

  std::cout << "All FRI tests passed\n";
  return 0;
}
