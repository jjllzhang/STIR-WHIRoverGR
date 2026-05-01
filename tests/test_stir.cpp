#include <NTL/ZZ_pE.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "algebra/gr_context.hpp"
#include "algebra/teichmuller.hpp"
#include "crypto/fs/transcript.hpp"
#include "domain.hpp"
#include "poly_utils/polynomial.hpp"
#include "stir/prover.hpp"
#include "stir/soundness.hpp"
#include "stir/verifier.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;
std::filesystem::path g_test_binary_dir;

namespace {

using stir_whir_gr::Domain;
using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;
using stir_whir_gr::algebra::GRElem;
using stir_whir_gr::poly_utils::Polynomial;

std::uint64_t EncodedRingVectorBytes(const GRContext& ctx,
                                     std::size_t value_count) {
  return sizeof(std::uint64_t) +
         static_cast<std::uint64_t>(value_count) *
             (sizeof(std::uint64_t) +
              static_cast<std::uint64_t>(ctx.elem_bytes()));
}

std::uint64_t EncodedPolynomialBytes(const GRContext& ctx,
                                     const Polynomial& polynomial) {
  return EncodedRingVectorBytes(ctx, polynomial.coefficients().size());
}

std::uint64_t LegacyRawStirBytes(const GRContext& ctx,
                                 const stir_whir_gr::stir::StirProofWithWitness& artifact) {
  std::uint64_t bytes = stir_whir_gr::stir::serialized_message_bytes(ctx, artifact.proof);
  bytes += sizeof(std::uint64_t);
  for (const auto& round_witness : artifact.witness.rounds) {
    bytes += EncodedPolynomialBytes(ctx, round_witness.input_polynomial);
    bytes += EncodedPolynomialBytes(ctx, round_witness.folded_polynomial);
    bytes += EncodedRingVectorBytes(ctx, round_witness.shifted_oracle_evals.size());
    bytes += EncodedPolynomialBytes(ctx, round_witness.answer_polynomial);
    bytes += EncodedPolynomialBytes(ctx, round_witness.vanishing_polynomial);
    bytes += EncodedPolynomialBytes(ctx, round_witness.quotient_polynomial);
    bytes += EncodedPolynomialBytes(ctx, round_witness.next_polynomial);
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

stir_whir_gr::stir::StirParameters MakeParams(
    std::vector<std::uint64_t> query_repetitions = {1},
    std::uint64_t stop_degree = 3) {
  stir_whir_gr::stir::StirParameters params;
  params.virtual_fold_factor = 9;
  params.shift_power = 3;
  params.ood_samples = 2;
  params.query_repetitions = std::move(query_repetitions);
  params.stop_degree = stop_degree;
  params.lambda_target = 64;
  params.pow_bits = 0;
  params.sec_mode = stir_whir_gr::SecurityMode::ConjectureCapacity;
  params.hash_profile = stir_whir_gr::HashProfile::STIR_NATIVE;
  return params;
}

stir_whir_gr::stir::StirParameters MakeAutoParams(std::uint64_t stop_degree = 3) {
  return MakeParams({}, stop_degree);
}

stir_whir_gr::stir::StirParameters MakeTheoremParams(
    std::vector<std::uint64_t> query_repetitions = {1},
    std::uint64_t stop_degree = 3) {
  auto params = MakeParams(std::move(query_repetitions), stop_degree);
  params.protocol_mode = stir_whir_gr::stir::StirProtocolMode::TheoremGr;
  params.challenge_sampling = stir_whir_gr::stir::StirChallengeSampling::TeichmullerT;
  params.ood_sampling =
      stir_whir_gr::stir::StirOodSamplingMode::TheoremExceptionalComplementUnique;
  return params;
}

stir_whir_gr::stir::StirInstance MakeInstance(const GRContext& ctx,
                                      std::uint64_t domain_size = 27,
                                      std::uint64_t claimed_degree = 26) {
  return stir_whir_gr::stir::StirInstance{
      .domain = Domain::teichmuller_subgroup(ctx, domain_size),
      .claimed_degree = claimed_degree,
  };
}

void NudgePolynomial(const GRContext& ctx, Polynomial* polynomial) {
  ctx.with_ntl_context([&] {
    auto coefficients = polynomial->coefficients();
    if (coefficients.empty()) {
      coefficients.push_back(ctx.one());
    } else {
      coefficients[0] += ctx.one();
    }
    *polynomial = Polynomial(std::move(coefficients));
    return 0;
  });
}

void FlipFirstByte(std::vector<std::uint8_t>* bytes) {
  if (bytes->empty()) {
    bytes->push_back(1U);
  } else {
    (*bytes)[0] ^= 0x01U;
  }
}

void TamperInitialRoot(stir_whir_gr::stir::StirProof* proof) {
  FlipFirstByte(&proof->initial_root);
}

void TamperPrevQueryPayload(stir_whir_gr::stir::StirProof* proof) {
  FlipFirstByte(&proof->rounds[0].queries_to_prev.leaf_payloads[0]);
}

void TamperGRoot(stir_whir_gr::stir::StirProof* proof) { FlipFirstByte(&proof->rounds[0].g_root); }

void TamperBeta(const GRContext& ctx, stir_whir_gr::stir::StirProof* proof) {
  ctx.with_ntl_context([&] {
    proof->rounds[0].betas[0] += ctx.one();
    return 0;
  });
}

void TamperAnsPolynomial(const GRContext& ctx, stir_whir_gr::stir::StirProof* proof) {
  NudgePolynomial(ctx, &proof->rounds[0].ans_polynomial);
}

void TamperShakePolynomial(const GRContext& ctx,
                           stir_whir_gr::stir::StirProof* proof) {
  NudgePolynomial(ctx, &proof->rounds[0].shake_polynomial);
}

void TamperFinalPolynomial(const GRContext& ctx,
                           stir_whir_gr::stir::StirProof* proof) {
  NudgePolynomial(ctx, &proof->final_polynomial);
}

void TamperFinalQueryPayload(stir_whir_gr::stir::StirProof* proof) {
  FlipFirstByte(&proof->queries_to_final.leaf_payloads[0]);
}

void TamperCompatWitness(const GRContext& ctx,
                         stir_whir_gr::stir::StirProofWithWitness* artifact) {
  NudgePolynomial(ctx, &artifact->witness.rounds[0].input_polynomial);
}

bool ContainsSubstring(const std::vector<std::string>& values,
                       std::string_view needle) {
  return std::any_of(values.begin(), values.end(), [&](const std::string& value) {
    return value.find(needle) != std::string::npos;
  });
}

std::string ShellSingleQuote(std::string_view value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted += "'";
  return quoted;
}

std::string RunBenchTimeText(const std::vector<std::string>& args) {
  const auto bench_path = (g_test_binary_dir / "bench_time").string();
  std::string command = ShellSingleQuote(bench_path);
  for (const auto& arg : args) {
    command += " ";
    command += ShellSingleQuote(arg);
  }
  command += " 2>&1";

  std::array<char, 256> buffer{};
  std::string output;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    throw std::runtime_error("failed to spawn bench_time");
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  const int status = pclose(pipe);
  if (status != 0) {
    throw std::runtime_error("bench_time failed: " + output);
  }
  return output;
}

void TestStir9to3HonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "stir-9to3 honest prover/verifier passes on the proof-only route");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  const auto artifact = prover.prove_with_witness(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK(verifier.verify(instance, artifact));
  CHECK_EQ(proof.initial_root, artifact.proof.initial_root);
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{1});
  CHECK_EQ(proof.rounds.size(), std::size_t{1});
  CHECK(!proof.initial_root.empty());
  CHECK(!proof.rounds[0].g_root.empty());
  CHECK_EQ(proof.rounds[0].betas.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].queries_to_prev.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.queries_to_final.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[0].ans_polynomial.coefficients(),
           artifact.witness.rounds[0].answer_polynomial.coefficients());
  CHECK(proof.final_polynomial.degree() <= std::size_t{0});
  CHECK_EQ(proof.stats.serialized_bytes,
           stir_whir_gr::stir::serialized_message_bytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawStirBytes(ctx, artifact));
}

void TestStirMultiRoundUsesPublicRootChain() {
  testutil::PrintInfo(
      "multi-round stir maintains the public initial-root to g-root chain");

  const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
  const auto instance = MakeInstance(ctx, 243, 161);
  auto params = MakeParams({1, 1}, 3);
  params.ood_samples = 1;
  const auto polynomial = SamplePolynomial(
      ctx, instance.domain, static_cast<std::size_t>(instance.claimed_degree + 1));

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  const auto artifact = prover.prove_with_witness(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{2});
  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].queries_to_prev.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[1].queries_to_prev.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.queries_to_final.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.stats.serialized_bytes,
           stir_whir_gr::stir::serialized_message_bytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawStirBytes(ctx, artifact));
}

void TestStirZeroRoundFinalFoldStillVerifies() {
  testutil::PrintInfo(
      "zero-round stir still verifies via initial-root to final-fold consistency");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams({1}, 9);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.rounds.size(), std::size_t{0});
  CHECK(!proof.initial_root.empty());
  CHECK_EQ(proof.queries_to_final.queried_indices.size(), std::size_t{1});
  CHECK(proof.final_polynomial.degree() <= std::size_t{0});
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

  const stir_whir_gr::stir::StirProver prover(auto_params);
  const stir_whir_gr::stir::StirVerifier verifier(auto_params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].queries_to_prev.queried_indices.size(),
           std::size_t{2});
  CHECK_EQ(proof.rounds[1].queries_to_prev.queried_indices.size(),
           std::size_t{1});
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

  const stir_whir_gr::stir::StirProver auto_prover(auto_params);
  const stir_whir_gr::stir::StirVerifier auto_verifier(auto_params);
  const auto auto_proof = auto_prover.prove(instance, polynomial);

  const stir_whir_gr::stir::StirProver manual_prover(manual_params);
  const stir_whir_gr::stir::StirVerifier manual_verifier(manual_params);
  const auto manual_proof = manual_prover.prove(instance, polynomial);

  CHECK(auto_verifier.verify(instance, auto_proof));
  CHECK(manual_verifier.verify(instance, manual_proof));
  CHECK_EQ(auto_proof.rounds[0].queries_to_prev.queried_indices.size(),
           std::size_t{2});
  CHECK_EQ(auto_proof.rounds[1].queries_to_prev.queried_indices.size(),
           std::size_t{1});
  CHECK_EQ(manual_proof.rounds[0].queries_to_prev.queried_indices.size(),
           std::size_t{1});
  CHECK_EQ(manual_proof.rounds[1].queries_to_prev.queried_indices.size(),
           std::size_t{1});
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
      stir_whir_gr::stir::resolve_query_schedule_metadata(params, instance);
  CHECK_EQ(schedule_metadata.size(), std::size_t{1});
  CHECK_EQ(schedule_metadata[0].requested_query_count, std::uint64_t{10});
  CHECK_EQ(schedule_metadata[0].bundle_count, std::uint64_t{3});
  CHECK_EQ(schedule_metadata[0].effective_query_count, std::uint64_t{1});
  CHECK(schedule_metadata[0].cap_applied);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.rounds.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[0].queries_to_prev.queried_indices.size(),
           std::size_t{1});
}

void TestStirExceptionalSetsStayUnitDifferenceSafe() {
  testutil::PrintInfo(
      "stir ood points and round domains satisfy the verifier's unit-difference precondition");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 26);
  const auto folded_domain = instance.domain.pow_map(9);
  const auto shift_domain = instance.domain.scale_offset(3);
  const std::vector<std::uint8_t> seed_material{0x53, 0x57, 0x47, 0x52};

  CHECK(stir_whir_gr::stir::domains_have_unit_differences(shift_domain, folded_domain));

  const auto ood_points = stir_whir_gr::stir::derive_ood_points(
      instance.domain, shift_domain, folded_domain, seed_material, 0, 2);
  CHECK(stir_whir_gr::stir::points_have_unit_differences(shift_domain, ood_points));
  CHECK(stir_whir_gr::stir::points_have_unit_differences(folded_domain, ood_points));

  const std::vector<GRElem> bad_points{shift_domain.element(0)};
  CHECK(!stir_whir_gr::stir::points_have_unit_differences(shift_domain, bad_points));
}

void TestStirTheoremChallengesLieInTeichmullerSet() {
  testutil::PrintInfo(
      "stir theorem folding and comb challenges are sampled from T");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  stir_whir_gr::crypto::Transcript transcript(stir_whir_gr::HashProfile::STIR_NATIVE);

  for (std::size_t round_index = 0; round_index < 16; ++round_index) {
    const std::vector<std::uint8_t> root_bytes{
        static_cast<std::uint8_t>(0x40U + round_index)};
    transcript.absorb_bytes(root_bytes);
    const auto folding = stir_whir_gr::stir::derive_stir_folding_challenge(
        transcript, ctx, "stir.fold_alpha:" + std::to_string(round_index));
    const auto comb = stir_whir_gr::stir::derive_stir_comb_challenge(
        transcript, ctx, "stir.comb:" + std::to_string(round_index));
    CHECK(stir_whir_gr::algebra::is_teichmuller_element(ctx, folding));
    CHECK(stir_whir_gr::algebra::is_teichmuller_element(ctx, comb));
  }
}

void TestStirTeichmullerUnitSubsetHelper() {
  testutil::PrintInfo(
      "stir teichmuller-unit helper rejects non-teich cosets and excludes zero");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain subgroup = Domain::teichmuller_subgroup(ctx, 9);
  const Domain teich_coset =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 9);
  const auto non_teich_unit = ctx.with_ntl_context([&] {
    auto three = ctx.one();
    three += ctx.one();
    three += ctx.one();
    return three;
  });
  const Domain non_teich_coset =
      Domain::teichmuller_coset(ctx, non_teich_unit, 9);

  CHECK(stir_whir_gr::stir::domain_is_subset_of_teichmuller_units(subgroup));
  CHECK(stir_whir_gr::stir::domain_is_subset_of_teichmuller_units(teich_coset));
  CHECK(!stir_whir_gr::stir::domain_is_subset_of_teichmuller_units(non_teich_coset));
}

void TestStirTheoremExceptionalSamplersStayInsideTeichmullerUnits() {
  testutil::PrintInfo(
      "stir theorem OOD and shake samplers draw from an explicit T* safe complement");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 26);
  const auto folded_domain = instance.domain.pow_map(9);
  const auto shift_domain = instance.domain.scale_offset(3);
  stir_whir_gr::crypto::Transcript ood_transcript(stir_whir_gr::HashProfile::STIR_NATIVE);
  stir_whir_gr::crypto::Transcript shake_transcript(stir_whir_gr::HashProfile::STIR_NATIVE);

  const auto ood_points = stir_whir_gr::stir::derive_theorem_ood_points(
      instance.domain, shift_domain, folded_domain, ood_transcript,
      "stir.theorem.ood", 2);
  CHECK_EQ(ood_points.size(), std::size_t{2});
  CHECK(stir_whir_gr::stir::theorem_ood_pool_has_capacity(
      instance.domain, shift_domain, folded_domain, 2));
  for (const auto& point : ood_points) {
    CHECK(stir_whir_gr::algebra::is_teichmuller_element(ctx, point));
    CHECK(ctx.is_unit(point));
    CHECK(!instance.domain.contains(point));
    CHECK(!shift_domain.contains(point));
    CHECK(!folded_domain.contains(point));
  }

  std::vector<GRElem> quotient_points = ood_points;
  quotient_points.push_back(folded_domain.element(0));
  quotient_points.push_back(folded_domain.element(1));
  const auto shake_point = stir_whir_gr::stir::derive_theorem_shake_point(
      instance.domain, shift_domain, folded_domain, quotient_points,
      shake_transcript, "stir.theorem.shake");
  CHECK(stir_whir_gr::stir::theorem_shake_pool_has_capacity(
      instance.domain, shift_domain, folded_domain, quotient_points));
  CHECK(stir_whir_gr::algebra::is_teichmuller_element(ctx, shake_point));
  CHECK(ctx.is_unit(shake_point));
  CHECK(!instance.domain.contains(shake_point));
  CHECK(!shift_domain.contains(shake_point));
  CHECK(!folded_domain.contains(shake_point));
  CHECK(std::find(quotient_points.begin(), quotient_points.end(), shake_point) ==
        quotient_points.end());

  const auto theorem_params = MakeTheoremParams();
  stir_whir_gr::crypto::Transcript wrapped_ood_transcript(stir_whir_gr::HashProfile::STIR_NATIVE);
  stir_whir_gr::crypto::Transcript direct_ood_transcript(stir_whir_gr::HashProfile::STIR_NATIVE);
  const auto wrapped_ood_points = stir_whir_gr::stir::derive_ood_points(
      theorem_params, instance.domain, shift_domain, folded_domain,
      wrapped_ood_transcript, "stir.theorem.ood.dispatch", 2);
  const auto direct_ood_points = stir_whir_gr::stir::derive_theorem_ood_points(
      instance.domain, shift_domain, folded_domain, direct_ood_transcript,
      "stir.theorem.ood.dispatch", 2);
  CHECK_EQ(wrapped_ood_points.size(), direct_ood_points.size());
  for (std::size_t i = 0; i < wrapped_ood_points.size(); ++i) {
    CHECK_EQ(wrapped_ood_points[i], direct_ood_points[i]);
  }

  stir_whir_gr::crypto::Transcript wrapped_shake_transcript(stir_whir_gr::HashProfile::STIR_NATIVE);
  stir_whir_gr::crypto::Transcript direct_shake_transcript(stir_whir_gr::HashProfile::STIR_NATIVE);
  const auto wrapped_shake_point = stir_whir_gr::stir::derive_shake_point(
      theorem_params, instance.domain, shift_domain, folded_domain,
      quotient_points, wrapped_shake_transcript, "stir.theorem.shake.dispatch");
  const auto direct_shake_dispatch = stir_whir_gr::stir::derive_theorem_shake_point(
      instance.domain, shift_domain, folded_domain, quotient_points,
      direct_shake_transcript, "stir.theorem.shake.dispatch");
  CHECK_EQ(wrapped_shake_point, direct_shake_dispatch);
}

void TestStirTheoremValidationRejectsBadDomainsAndExhaustedPools() {
  testutil::PrintInfo(
      "stir theorem validation rejects non-T* domains and exhausted safe complements");

  {
    const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
    const auto non_teich_unit = ctx.with_ntl_context([&] {
      auto three = ctx.one();
      three += ctx.one();
      three += ctx.one();
      return three;
    });
    const stir_whir_gr::stir::StirInstance bad_instance{
        .domain = Domain::teichmuller_coset(ctx, non_teich_unit, 9),
        .claimed_degree = 8,
    };

    CHECK(!stir_whir_gr::stir::domain_is_subset_of_teichmuller_units(
        bad_instance.domain));
    CHECK(!stir_whir_gr::stir::validate(MakeTheoremParams({1}, 3), bad_instance));
  }

  {
    const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
    const stir_whir_gr::stir::StirInstance saturated_instance{
        .domain = Domain::teichmuller_subgroup(ctx, 486),
        .claimed_degree = 485,
    };
    const auto shift_domain = saturated_instance.domain.scale_offset(3);
    const auto folded_domain = saturated_instance.domain.pow_map(9);
    std::vector<GRElem> quotient_points{folded_domain.element(0)};

    CHECK(stir_whir_gr::stir::domain_is_subset_of_teichmuller_units(
        saturated_instance.domain));
    CHECK(stir_whir_gr::stir::domain_is_subset_of_teichmuller_units(shift_domain));
    CHECK(stir_whir_gr::stir::domain_is_subset_of_teichmuller_units(folded_domain));
    CHECK(!stir_whir_gr::stir::theorem_ood_pool_has_capacity(
        saturated_instance.domain, shift_domain, folded_domain, 1));
    CHECK(!stir_whir_gr::stir::theorem_shake_pool_has_capacity(
        saturated_instance.domain, shift_domain, folded_domain,
        quotient_points));

    auto theorem_params = MakeTheoremParams({1}, 3);
    theorem_params.ood_samples = 1;
    CHECK(!stir_whir_gr::stir::validate(theorem_params, saturated_instance));
  }
}

void TestStirTheoremModeHonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "theorem-mode stir honest prover/verifier passes without changing proof shape");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeTheoremParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);
  const auto artifact = prover.prove_with_witness(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK(verifier.verify(instance, artifact));
  CHECK_EQ(proof.initial_root, artifact.proof.initial_root);
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{1});
  CHECK_EQ(proof.rounds.size(), std::size_t{1});
  CHECK(!proof.initial_root.empty());
  CHECK(!proof.rounds[0].g_root.empty());
  CHECK_EQ(proof.rounds[0].betas.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].queries_to_prev.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.queries_to_final.queried_indices.size(), std::size_t{1});
  CHECK(proof.final_polynomial.degree() <= std::size_t{0});
  CHECK_EQ(proof.stats.serialized_bytes,
           stir_whir_gr::stir::serialized_message_bytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawStirBytes(ctx, artifact));
}

void TestStirTheoremModeMultiRoundUsesPublicRootChain() {
  testutil::PrintInfo(
      "theorem-mode multi-round stir keeps the existing public proof shape");

  const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
  const auto instance = MakeInstance(ctx, 243, 161);
  auto params = MakeTheoremParams({1, 1}, 3);
  params.ood_samples = 1;
  const auto polynomial = SamplePolynomial(
      ctx, instance.domain, static_cast<std::size_t>(instance.claimed_degree + 1));

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{2});
  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].queries_to_prev.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.rounds[1].queries_to_prev.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.queries_to_final.queried_indices.size(), std::size_t{1});
  CHECK_EQ(proof.stats.serialized_bytes,
           stir_whir_gr::stir::serialized_message_bytes(ctx, proof));
}

void TestStirTheoremSoundnessAnalysisComputesOnSupportedInstance() {
  testutil::PrintInfo(
      "theorem stir soundness analysis computes a half-gap supported bound");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 54});
  const stir_whir_gr::stir::StirInstance instance{
      .domain = Domain::teichmuller_subgroup(ctx, 81),
      .claimed_degree = 26,
  };
  auto params = MakeTheoremParams({2, 4}, 3);
  params.ood_samples = 1;

  CHECK(stir_whir_gr::stir::validate(params, instance));

  const auto analysis = stir_whir_gr::stir::analyze_theorem_soundness(params, instance);
  CHECK(analysis.feasible);
  CHECK_EQ(
      analysis.flavor,
      stir_whir_gr::stir::StirTheoremSoundnessFlavor::GrHalfGapUniqueOod);
  CHECK_EQ(analysis.proximity_gap_model,
           std::string("z2ksnark_gr_half_gap_envelope_s_times_ell_sq_over_T"));
  CHECK_EQ(analysis.ood_model,
           std::string("unique_decoding_exceptional_complement"));
  CHECK_EQ(analysis.rounds.size(), std::size_t{1});
  CHECK(analysis.rounds.empty() || analysis.rounds[0].epsilon_out == 0.0L);
  CHECK(analysis.epsilon_fold > 0.0);
  CHECK(analysis.epsilon_fin > 0.0);
  CHECK_EQ(analysis.effective_security_bits, std::uint64_t{0});
  CHECK(ContainsSubstring(analysis.assumptions, "half-gap") ||
        ContainsSubstring(analysis.assumptions, "Z2KSNARK"));
}

void TestStirTheoremSoundnessAnalysisRejectsUnsupportedRegimes() {
  testutil::PrintInfo(
      "theorem stir soundness analysis marks oversized or trivial half-gap regimes unsupported");

  const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
  const auto instance = MakeInstance(ctx, 243, 161);
  auto params = MakeTheoremParams({1, 1}, 3);
  params.ood_samples = 1;

  CHECK(stir_whir_gr::stir::validate(params, instance));

  const auto analysis = stir_whir_gr::stir::analyze_theorem_soundness(params, instance);
  CHECK(!analysis.feasible);
  CHECK_EQ(
      analysis.flavor,
      stir_whir_gr::stir::StirTheoremSoundnessFlavor::GrHalfGapUniqueOod);
  CHECK(analysis.rounds.size() <= std::size_t{2});
  CHECK(analysis.rounds.empty() || analysis.rounds[0].epsilon_out == 0.0L);
  CHECK(analysis.epsilon_fold >= 0.0L);
  CHECK(analysis.effective_security_bits == std::uint64_t{0});
  CHECK(ContainsSubstring(analysis.assumptions, "Unsupported") ||
        ContainsSubstring(analysis.assumptions, "trivial") ||
        (!analysis.rounds.empty() && ContainsSubstring(analysis.rounds[0].notes, "Unsupported")) ||
        (!analysis.rounds.empty() && ContainsSubstring(analysis.rounds[0].notes, "trivial")));
}

void TestStirTheoremQuerySolverFindsMinimalScheduleOnSupportedInstance() {
  testutil::PrintInfo(
      "theorem stir query solver finds a minimal supported explicit schedule");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 54});
  const stir_whir_gr::stir::StirInstance instance{
      .domain = Domain::teichmuller_subgroup(ctx, 81),
      .claimed_degree = 26,
  };
  auto params = MakeTheoremParams({}, 3);
  params.lambda_target = 1;
  params.ood_samples = 1;

  const auto solved =
      stir_whir_gr::stir::solve_min_query_schedule_for_lambda(params, instance);
  CHECK(solved.feasible);
  CHECK_EQ(solved.analysis.effective_security_bits, std::uint64_t{1});
  CHECK_EQ(solved.query_schedule.size(), std::size_t{2});
  CHECK_EQ(solved.query_schedule[0], std::uint64_t{2});
  CHECK_EQ(solved.query_schedule[1], std::uint64_t{5});
}

void TestStirTheoremQuerySolverReportsInfeasibleMainWorkload() {
  testutil::PrintInfo(
      "theorem stir query solver reports infeasible lambda targets on unsupported main-workload style instances");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const stir_whir_gr::stir::StirInstance instance{
      .domain = Domain::teichmuller_subgroup(ctx, 243),
      .claimed_degree = 81,
  };
  auto params = MakeTheoremParams({}, 9);
  params.lambda_target = 128;
  params.ood_samples = 2;

  const auto solved =
      stir_whir_gr::stir::solve_min_query_schedule_for_lambda(params, instance);
  CHECK(!solved.feasible);
  CHECK(!solved.query_schedule.empty());
  CHECK_EQ(solved.analysis.effective_security_bits, std::uint64_t{4});
}

void TestStirRejectsTamperedPrevQueryOpening() {
  testutil::PrintInfo("stir verifier rejects a tampered previous-oracle opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperPrevQueryPayload(&proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedInitialRoot() {
  testutil::PrintInfo("stir verifier rejects a tampered initial root");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperInitialRoot(&proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedGRoot() {
  testutil::PrintInfo("stir verifier rejects a tampered g-root");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperGRoot(&proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedBeta() {
  testutil::PrintInfo("stir verifier rejects a tampered OOD beta");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperBeta(ctx, &proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedAnsPolynomial() {
  testutil::PrintInfo("stir verifier rejects a tampered ans polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperAnsPolynomial(ctx, &proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedShakePolynomial() {
  testutil::PrintInfo("stir verifier rejects a tampered shake polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperShakePolynomial(ctx, &proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedFinalPolynomial() {
  testutil::PrintInfo("stir verifier rejects a tampered final polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperFinalPolynomial(ctx, &proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirRejectsTamperedFinalOpening() {
  testutil::PrintInfo("stir verifier rejects a tampered final sparse opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperFinalQueryPayload(&proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestStirCompatWitnessNoLongerDrivesVerification() {
  testutil::PrintInfo(
      "stir compatibility witness no longer affects proof-only verification");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 27);

  const stir_whir_gr::stir::StirProver prover(params);
  const stir_whir_gr::stir::StirVerifier verifier(params);
  auto artifact = prover.prove_with_witness(instance, polynomial);

  TamperCompatWitness(ctx, &artifact);

  CHECK(verifier.verify(instance, artifact));
}

void TestStirValidationRejectsBadInputs() {
  testutil::PrintInfo(
      "stir validation rejects bad parameters and accepts safely capped schedules");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx);
  CHECK(stir_whir_gr::stir::validate(MakeParams(), instance));

  auto bad_fold = MakeParams();
  bad_fold.virtual_fold_factor = 3;
  CHECK(!stir_whir_gr::stir::validate(bad_fold));

  auto bad_shift = MakeParams();
  bad_shift.shift_power = 9;
  CHECK(!stir_whir_gr::stir::validate(bad_shift));

  auto bad_queries = MakeParams();
  bad_queries.query_repetitions = {1, 0};
  CHECK(!stir_whir_gr::stir::validate(bad_queries));

  const stir_whir_gr::stir::StirParameters default_params;
  CHECK_EQ(default_params.protocol_mode,
           stir_whir_gr::stir::StirProtocolMode::PrototypeEngineering);
  CHECK_EQ(default_params.challenge_sampling,
           stir_whir_gr::stir::StirChallengeSampling::AmbientRing);
  CHECK_EQ(default_params.ood_sampling,
           stir_whir_gr::stir::StirOodSamplingMode::PrototypeShiftedCoset);

  auto theorem_with_ambient_challenge = MakeTheoremParams();
  theorem_with_ambient_challenge.challenge_sampling =
      stir_whir_gr::stir::StirChallengeSampling::AmbientRing;
  CHECK(!stir_whir_gr::stir::validate(theorem_with_ambient_challenge));

  auto theorem_with_prototype_ood = MakeTheoremParams();
  theorem_with_prototype_ood.ood_sampling =
      stir_whir_gr::stir::StirOodSamplingMode::PrototypeShiftedCoset;
  CHECK(!stir_whir_gr::stir::validate(theorem_with_prototype_ood));

  CHECK(stir_whir_gr::stir::validate(MakeTheoremParams(), instance));

  const stir_whir_gr::stir::StirInstance bad_degree{
      .domain = instance.domain,
      .claimed_degree = instance.domain.size(),
  };
  CHECK(!stir_whir_gr::stir::validate(MakeParams(), bad_degree));
  CHECK(stir_whir_gr::stir::domains_have_unit_differences(
      instance.domain.scale_offset(3), instance.domain.pow_map(9)));

  auto too_many_queries = MakeParams({2});
  CHECK(stir_whir_gr::stir::validate(too_many_queries, instance));

  auto no_query_budget = MakeParams({1});
  no_query_budget.ood_samples = 3;
  CHECK(!stir_whir_gr::stir::validate(no_query_budget, instance));

  const auto tiny_instance = MakeInstance(ctx, 3, 2);
  CHECK(!stir_whir_gr::stir::validate(MakeParams(), tiny_instance));
}

void TestStirBenchTimeEmitsTheoremMetadataAndUnsupportedZeroBits() {
  testutil::PrintInfo(
      "bench_time emits theorem-facing stir metadata and keeps unsupported rows at zero bits");

  const auto supported_output = RunBenchTimeText({
      "--protocol", "stir9to3",
      "--n", "81",
      "--d", "26",
      "--r", "54",
      "--stop-degree", "3",
      "--ood-samples", "1",
      "--queries", "2,4",
      "--lambda", "128",
      "--pow-bits", "0",
      "--format", "text",
      "--warmup", "0",
      "--reps", "1",
  });
  CHECK(supported_output.find("soundness_mode=theorem_gr") !=
        std::string::npos);
  CHECK(supported_output.find(
            "soundness_model=epsilon_rbr_stir_gr_half_gap_unique_ood") !=
        std::string::npos);
  CHECK(supported_output.find(
            "soundness_scope=theorem_gr_existing_z2ksnark_half_gap_results") !=
        std::string::npos);
  CHECK(supported_output.find(
            "pow_policy=benchmark_only_not_in_theorem_bound") !=
        std::string::npos);

  const auto theorem_auto_output = RunBenchTimeText({
      "--protocol", "stir9to3",
      "--n", "81",
      "--d", "26",
      "--r", "54",
      "--stop-degree", "3",
      "--ood-samples", "1",
      "--queries", "theorem_auto",
      "--lambda", "1",
      "--pow-bits", "0",
      "--format", "text",
      "--warmup", "0",
      "--reps", "1",
  });
  CHECK(theorem_auto_output.find("query_policy=theorem_auto_solved_schedule") !=
        std::string::npos);
  CHECK(theorem_auto_output.find("effective_security_bits=1") !=
        std::string::npos);

  const auto unsupported_output = RunBenchTimeText({
      "--protocol", "stir9to3",
      "--n", "243",
      "--d", "81",
      "--r", "162",
      "--lambda", "128",
      "--pow-bits", "0",
      "--format", "text",
      "--warmup", "0",
      "--reps", "1",
  });
  CHECK(unsupported_output.find("soundness_mode=theorem_gr") !=
        std::string::npos);
  CHECK(unsupported_output.find("effective_security_bits=0") !=
        std::string::npos);
  CHECK(unsupported_output.find("query_policy=auto_live_schedule") !=
        std::string::npos);
  CHECK(unsupported_output.find("unsupported") != std::string::npos ||
        unsupported_output.find("Unsupported") != std::string::npos);
}

void TestStirFreshContextsStayStableAcrossAutoAndManualSchedules() {
  testutil::PrintInfo(
      "fresh contexts keep stir auto and manual schedules stable after deterministic teich setup");

  std::vector<std::uint8_t> expected_generator;
  std::vector<std::uint8_t> expected_root;
  for (int iteration = 0; iteration < 6; ++iteration) {
    const GRContext ctx(GRConfig{.p = 487, .k_exp = 2, .r = 1});
    const auto instance = MakeInstance(ctx, 243, 161);
    const auto polynomial = SamplePolynomial(
        ctx, instance.domain,
        static_cast<std::size_t>(instance.claimed_degree + 1));

    const auto generator_bytes = ctx.serialize(ctx.teich_generator());
    const auto root_bytes = ctx.serialize(instance.domain.root());
    if (iteration == 0) {
      expected_generator = generator_bytes;
      expected_root = root_bytes;
    } else {
      CHECK_EQ(generator_bytes, expected_generator);
      CHECK_EQ(root_bytes, expected_root);
    }

    auto auto_params = MakeAutoParams(3);
    auto_params.ood_samples = 1;
    auto manual_params = MakeParams({1, 1}, 3);
    manual_params.ood_samples = 1;

    const stir_whir_gr::stir::StirProver auto_prover(auto_params);
    const stir_whir_gr::stir::StirVerifier auto_verifier(auto_params);
    const auto auto_proof = auto_prover.prove(instance, polynomial);
    CHECK(auto_verifier.verify(instance, auto_proof));

    const stir_whir_gr::stir::StirProver manual_prover(manual_params);
    const stir_whir_gr::stir::StirVerifier manual_verifier(manual_params);
    const auto manual_proof = manual_prover.prove(instance, polynomial);
    CHECK(manual_verifier.verify(instance, manual_proof));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 0) {
    g_test_binary_dir = std::filesystem::absolute(argv[0]).parent_path();
  }
  try {
    RUN_TEST(TestStir9to3HonestRoundtripAndRoundShape);
    RUN_TEST(TestStirMultiRoundUsesPublicRootChain);
    RUN_TEST(TestStirZeroRoundFinalFoldStillVerifies);
    RUN_TEST(TestStirAutoScheduleMatchesConjectureCapacityDefault);
    RUN_TEST(TestStirManualQueriesOverrideAutoSchedule);
    RUN_TEST(TestStirCapsOversubscribedQueriesWithDegreeBudget);
    RUN_TEST(TestStirExceptionalSetsStayUnitDifferenceSafe);
    RUN_TEST(TestStirTheoremChallengesLieInTeichmullerSet);
    RUN_TEST(TestStirTeichmullerUnitSubsetHelper);
    RUN_TEST(TestStirTheoremExceptionalSamplersStayInsideTeichmullerUnits);
    RUN_TEST(TestStirTheoremValidationRejectsBadDomainsAndExhaustedPools);
    RUN_TEST(TestStirTheoremModeHonestRoundtripAndRoundShape);
    RUN_TEST(TestStirTheoremModeMultiRoundUsesPublicRootChain);
    RUN_TEST(TestStirTheoremSoundnessAnalysisComputesOnSupportedInstance);
    RUN_TEST(TestStirTheoremSoundnessAnalysisRejectsUnsupportedRegimes);
    RUN_TEST(TestStirTheoremQuerySolverFindsMinimalScheduleOnSupportedInstance);
    RUN_TEST(TestStirTheoremQuerySolverReportsInfeasibleMainWorkload);
    RUN_TEST(TestStirRejectsTamperedPrevQueryOpening);
    RUN_TEST(TestStirRejectsTamperedInitialRoot);
    RUN_TEST(TestStirRejectsTamperedGRoot);
    RUN_TEST(TestStirRejectsTamperedBeta);
    RUN_TEST(TestStirRejectsTamperedAnsPolynomial);
    RUN_TEST(TestStirRejectsTamperedShakePolynomial);
    RUN_TEST(TestStirRejectsTamperedFinalPolynomial);
    RUN_TEST(TestStirRejectsTamperedFinalOpening);
    RUN_TEST(TestStirCompatWitnessNoLongerDrivesVerification);
    RUN_TEST(TestStirValidationRejectsBadInputs);
    RUN_TEST(TestStirBenchTimeEmitsTheoremMetadataAndUnsupportedZeroBits);
    RUN_TEST(TestStirFreshContextsStayStableAcrossAutoAndManualSchedules);
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
