#include <NTL/ZZ_pE.h>

#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "fri/common.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/polynomial.hpp"
#include "stir/prover.hpp"
#include "stir/verifier.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

using swgr::Domain;
using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;
using swgr::poly_utils::Polynomial;

std::uint64_t MerkleOpeningPayloadBytes(
    const swgr::crypto::MerkleProof& proof) {
  std::uint64_t bytes = 0;
  for (const auto& payload : proof.leaf_payloads) {
    bytes += static_cast<std::uint64_t>(payload.size());
  }
  for (const auto& sibling : proof.sibling_hashes) {
    bytes += static_cast<std::uint64_t>(sibling.size());
  }
  return bytes;
}

std::uint64_t CompactStirBytes(const GRContext& ctx,
                               const swgr::stir::StirProof& proof) {
  std::uint64_t bytes = 0;
  for (std::size_t round_index = 0;
       round_index < proof.rounds.size() && round_index < proof.oracle_roots.size();
       ++round_index) {
    const auto& round = proof.rounds[round_index];
    bytes += static_cast<std::uint64_t>(proof.oracle_roots[round_index].size());
    bytes += static_cast<std::uint64_t>(round.ood_answers.size()) *
             static_cast<std::uint64_t>(ctx.elem_bytes());
    bytes += static_cast<std::uint64_t>(round.quotient_polynomial.coefficients().size()) *
             static_cast<std::uint64_t>(ctx.elem_bytes());
    bytes += MerkleOpeningPayloadBytes(round.input_oracle_proof);
    bytes += MerkleOpeningPayloadBytes(round.shift_oracle_proof);
  }
  return bytes;
}

std::uint64_t LegacyRawStirBytes(const GRContext& ctx,
                                 const swgr::stir::StirProof& proof) {
  std::uint64_t bytes = 0;
  for (std::size_t round_index = 0;
       round_index < proof.rounds.size() && round_index < proof.oracle_roots.size();
       ++round_index) {
    const auto& round = proof.rounds[round_index];
    bytes += static_cast<std::uint64_t>(round.shifted_oracle_evals.size()) *
             static_cast<std::uint64_t>(ctx.elem_bytes());
    bytes += static_cast<std::uint64_t>(round.ood_answers.size() +
                                        round.shift_query_answers.size()) *
             static_cast<std::uint64_t>(ctx.elem_bytes());
    bytes += static_cast<std::uint64_t>(round.answer_polynomial.coefficients().size() +
                                        round.vanishing_polynomial.coefficients().size() +
                                        round.quotient_polynomial.coefficients().size() +
                                        round.next_polynomial.coefficients().size() +
                                        round.input_polynomial.coefficients().size() +
                                        round.folded_polynomial.coefficients().size()) *
             static_cast<std::uint64_t>(ctx.elem_bytes());
    bytes += static_cast<std::uint64_t>(proof.oracle_roots[round_index].size());
    bytes += static_cast<std::uint64_t>(round.fold_query_positions.size()) *
             sizeof(std::uint64_t);
    bytes += static_cast<std::uint64_t>(round.shift_query_positions.size()) *
             sizeof(std::uint64_t);
    bytes += MerkleOpeningPayloadBytes(round.input_oracle_proof);
    bytes += MerkleOpeningPayloadBytes(round.shift_oracle_proof);
  }
  bytes += static_cast<std::uint64_t>(proof.final_polynomial.coefficients().size()) *
           static_cast<std::uint64_t>(ctx.elem_bytes());
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

swgr::stir::StirParameters MakeParams(
    std::vector<std::uint64_t> query_repetitions = {1},
    std::uint64_t stop_degree = 3) {
  swgr::stir::StirParameters params;
  params.virtual_fold_factor = 9;
  params.shift_power = 3;
  params.ood_samples = 2;
  params.query_repetitions = std::move(query_repetitions);
  params.stop_degree = stop_degree;
  params.lambda_target = 64;
  params.pow_bits = 0;
  params.sec_mode = swgr::SecurityMode::ConjectureCapacity;
  params.hash_profile = swgr::HashProfile::STIR_NATIVE;
  return params;
}

swgr::stir::StirParameters MakeAutoParams(std::uint64_t stop_degree = 3) {
  return MakeParams({}, stop_degree);
}

swgr::stir::StirInstance MakeInstance(const GRContext& ctx,
                                      std::uint64_t domain_size = 27,
                                      std::uint64_t claimed_degree = 26) {
  return swgr::stir::StirInstance{
      .domain = Domain::teichmuller_subgroup(ctx, domain_size),
      .claimed_degree = claimed_degree,
  };
}

void TamperShiftedOracle(const GRContext& ctx, swgr::stir::StirProof& proof) {
  ctx.with_ntl_context([&] {
    proof.rounds[0].shifted_oracle_evals[0] += ctx.one();
    return 0;
  });
}

void TamperOodAnswer(const GRContext& ctx, swgr::stir::StirProof& proof) {
  ctx.with_ntl_context([&] {
    proof.rounds[0].ood_answers[0] += ctx.one();
    return 0;
  });
}

void TamperNextPolynomial(const GRContext& ctx, swgr::stir::StirProof& proof) {
  ctx.with_ntl_context([&] {
    auto coefficients = proof.rounds[0].next_polynomial.coefficients();
    if (coefficients.empty()) {
      coefficients.push_back(ctx.one());
    } else {
      coefficients[0] += ctx.one();
    }
    proof.rounds[0].next_polynomial = Polynomial(std::move(coefficients));
    return 0;
  });
}

void TestStir9to3HonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "stir-9to3 honest prover/verifier passes and exposes one explicit round");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const swgr::stir::StirProver prover(params);
  const swgr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{1});
  CHECK_EQ(proof.rounds.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[0].input_domain_size, std::uint64_t{27});
  CHECK_EQ(proof.rounds[0].folded_domain_size, std::uint64_t{3});
  CHECK_EQ(proof.rounds[0].shift_domain_size, std::uint64_t{9});
  CHECK_EQ(proof.rounds[0].shift_query_positions.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[0].ood_points.size(), std::size_t{2});
  CHECK(proof.final_polynomial.degree() <= params.stop_degree);
  CHECK_EQ(proof.final_polynomial.coefficients(),
           proof.rounds[0].next_polynomial.coefficients());
  CHECK_EQ(proof.stats.serialized_bytes, CompactStirBytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawStirBytes(ctx, proof));
}

void TestStirMultiRoundCachesNextRoundInputOracle() {
  testutil::PrintInfo(
      "multi-round stir keeps next polynomial and next input oracle payloads aligned");

  const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
  const auto instance = MakeInstance(ctx, 243, 161);
  auto params = MakeParams({1, 1}, 3);
  params.ood_samples = 1;
  const auto polynomial = SamplePolynomial(
      ctx, instance.domain, static_cast<std::size_t>(instance.claimed_degree + 1));

  const swgr::stir::StirProver prover(params);
  const swgr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{2});
  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  CHECK_EQ(proof.stats.serialized_bytes, CompactStirBytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawStirBytes(ctx, proof));
  ctx.with_ntl_context([&] {
    CHECK_EQ(proof.rounds[1].input_polynomial.coefficients(),
             proof.rounds[0].next_polynomial.coefficients());

    const Domain round_one_shift_domain =
        instance.domain.scale_offset(params.shift_power);
    const auto expected_round_two_input_oracle =
        swgr::poly_utils::rs_encode(round_one_shift_domain,
                                    proof.rounds[1].input_polynomial);
    CHECK_EQ(proof.rounds[1].input_oracle_proof.leaf_payloads.size(),
             proof.rounds[1].fold_query_positions.size());
    for (std::size_t i = 0; i < proof.rounds[1].fold_query_positions.size(); ++i) {
      const auto position = proof.rounds[1].fold_query_positions[i];
      CHECK_EQ(proof.rounds[1].input_oracle_proof.leaf_payloads[i],
               swgr::fri::serialize_oracle_bundle(
                   ctx, expected_round_two_input_oracle,
                   params.virtual_fold_factor, position));
    }
    return 0;
  });
}

void TestStirAutoScheduleMatchesConjectureCapacityDefault() {
  testutil::PrintInfo(
      "stir auto query schedule matches the default conjecture-capacity round shape");

  const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
  const auto instance = MakeInstance(ctx, 243, 161);
  auto auto_params = MakeAutoParams(3);
  auto_params.ood_samples = 1;
  const auto polynomial = SamplePolynomial(
      ctx, instance.domain, static_cast<std::size_t>(instance.claimed_degree + 1));

  const swgr::stir::StirProver prover(auto_params);
  const swgr::stir::StirVerifier verifier(auto_params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].fold_query_positions.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].shift_query_positions.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[1].fold_query_positions.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[1].shift_query_positions.size(), std::size_t{1});
}

void TestStirManualQueriesOverrideAutoSchedule() {
  testutil::PrintInfo(
      "stir manual query schedule overrides conjecture-capacity auto scheduling");

  const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
  const auto instance = MakeInstance(ctx, 243, 161);
  const auto polynomial = SamplePolynomial(
      ctx, instance.domain, static_cast<std::size_t>(instance.claimed_degree + 1));

  auto auto_params = MakeAutoParams(3);
  auto_params.ood_samples = 1;
  auto manual_params = MakeParams({1, 1}, 3);
  manual_params.ood_samples = 1;

  const swgr::stir::StirProver auto_prover(auto_params);
  const swgr::stir::StirVerifier auto_verifier(auto_params);
  const auto auto_proof = auto_prover.prove(instance, polynomial);

  const swgr::stir::StirProver manual_prover(manual_params);
  const swgr::stir::StirVerifier manual_verifier(manual_params);
  const auto manual_proof = manual_prover.prove(instance, polynomial);

  CHECK(auto_verifier.verify(instance, auto_proof));
  CHECK(manual_verifier.verify(instance, manual_proof));
  CHECK_EQ(auto_proof.rounds[0].fold_query_positions.size(), std::size_t{2});
  CHECK_EQ(auto_proof.rounds[0].shift_query_positions.size(), std::size_t{2});
  CHECK_EQ(auto_proof.rounds[1].fold_query_positions.size(), std::size_t{1});
  CHECK_EQ(auto_proof.rounds[1].shift_query_positions.size(), std::size_t{1});
  CHECK_EQ(manual_proof.rounds[0].fold_query_positions.size(), std::size_t{1});
  CHECK_EQ(manual_proof.rounds[0].shift_query_positions.size(), std::size_t{1});
  CHECK_EQ(manual_proof.rounds[1].fold_query_positions.size(), std::size_t{1});
  CHECK_EQ(manual_proof.rounds[1].shift_query_positions.size(), std::size_t{1});
}

void TestStirCapsOversubscribedQueriesWithDegreeBudget() {
  testutil::PrintInfo(
      "stir caps oversubscribed queries to a safe effective count under degree budget");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 26);
  auto params = MakeParams({10}, 3);
  params.ood_samples = 2;
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const auto schedule_metadata =
      swgr::stir::resolve_query_schedule_metadata(params, instance);
  CHECK_EQ(schedule_metadata.size(), std::size_t{1});
  CHECK_EQ(schedule_metadata[0].requested_query_count, std::uint64_t{10});
  CHECK_EQ(schedule_metadata[0].bundle_count, std::uint64_t{3});
  CHECK_EQ(schedule_metadata[0].effective_query_count, std::uint64_t{1});
  CHECK(schedule_metadata[0].cap_applied);

  const swgr::stir::StirProver prover(params);
  const swgr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.rounds.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[0].fold_query_positions.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[0].shift_query_positions.size(), std::size_t{1});
}

void TestStirRejectsTamperedShiftedOracle() {
  testutil::PrintInfo("stir verifier rejects a tampered shifted oracle opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const swgr::stir::StirProver prover(params);
  const swgr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperShiftedOracle(ctx, proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedOodAnswer() {
  testutil::PrintInfo("stir verifier rejects a tampered OOD answer");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const swgr::stir::StirProver prover(params);
  const swgr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperOodAnswer(ctx, proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedDegreeCorrection() {
  testutil::PrintInfo("stir verifier rejects a tampered degree-correction step");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const swgr::stir::StirProver prover(params);
  const swgr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperNextPolynomial(ctx, proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirValidationRejectsBadInputs() {
  testutil::PrintInfo(
      "stir validation rejects bad parameters and accepts safely capped schedules");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  CHECK(swgr::stir::validate(MakeParams(), instance));

  auto bad_fold = MakeParams();
  bad_fold.virtual_fold_factor = 3;
  CHECK(!swgr::stir::validate(bad_fold));

  auto bad_shift = MakeParams();
  bad_shift.shift_power = 9;
  CHECK(!swgr::stir::validate(bad_shift));

  auto bad_queries = MakeParams();
  bad_queries.query_repetitions = {1, 0};
  CHECK(!swgr::stir::validate(bad_queries));

  const swgr::stir::StirInstance bad_degree{
      .domain = instance.domain,
      .claimed_degree = instance.domain.size(),
  };
  CHECK(!swgr::stir::validate(MakeParams(), bad_degree));

  auto too_many_queries = MakeParams({2});
  CHECK(swgr::stir::validate(too_many_queries, instance));

  auto no_query_budget = MakeParams({1});
  no_query_budget.ood_samples = 3;
  CHECK(!swgr::stir::validate(no_query_budget, instance));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestStir9to3HonestRoundtripAndRoundShape);
    RUN_TEST(TestStirMultiRoundCachesNextRoundInputOracle);
    RUN_TEST(TestStirAutoScheduleMatchesConjectureCapacityDefault);
    RUN_TEST(TestStirManualQueriesOverrideAutoSchedule);
    RUN_TEST(TestStirCapsOversubscribedQueriesWithDegreeBudget);
    RUN_TEST(TestStirRejectsTamperedShiftedOracle);
    RUN_TEST(TestStirRejectsTamperedOodAnswer);
    RUN_TEST(TestStirRejectsTamperedDegreeCorrection);
    RUN_TEST(TestStirValidationRejectsBadInputs);
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
