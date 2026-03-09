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

std::uint64_t CompactFriBytes(const GRContext& ctx,
                              const swgr::fri::FriProof& proof) {
  std::uint64_t bytes = 0;
  const std::size_t query_rounds =
      proof.rounds.empty() ? 0 : proof.rounds.size() - 1U;
  for (std::size_t round_index = 0;
       round_index < query_rounds && round_index < proof.oracle_roots.size();
       ++round_index) {
    bytes += static_cast<std::uint64_t>(proof.oracle_roots[round_index].size());
    bytes += MerkleOpeningPayloadBytes(proof.rounds[round_index].oracle_proof);
  }
  bytes += static_cast<std::uint64_t>(proof.final_polynomial.coefficients().size()) *
           static_cast<std::uint64_t>(ctx.elem_bytes());
  return bytes;
}

std::uint64_t LegacyRawFriBytes(const GRContext& ctx,
                                const swgr::fri::FriProof& proof) {
  std::uint64_t bytes = 0;
  for (const auto& oracle_root : proof.oracle_roots) {
    bytes += static_cast<std::uint64_t>(oracle_root.size());
  }
  for (const auto& round : proof.rounds) {
    bytes += static_cast<std::uint64_t>(round.oracle_evals.size()) *
             static_cast<std::uint64_t>(ctx.elem_bytes());
    bytes += static_cast<std::uint64_t>(round.query_positions.size()) *
             sizeof(std::uint64_t);
    bytes += MerkleOpeningPayloadBytes(round.oracle_proof);
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

void TamperFirstQueriedOpening(const GRContext& ctx, swgr::fri::FriProof& proof) {
  const std::uint64_t index = proof.rounds[0].query_positions.front();
  ctx.with_ntl_context([&] {
    proof.rounds[0].oracle_evals[static_cast<std::size_t>(index)] += ctx.one();
    return 0;
  });
}

void TamperTerminalOracleTable(const GRContext& ctx, swgr::fri::FriProof& proof) {
  ctx.with_ntl_context([&] {
    proof.rounds.back().oracle_evals[0] += ctx.one();
    return 0;
  });
}

// Phase 0 baseline: FRI still exposes full `oracle_evals` tables in the proof,
// and the verifier consumes them for queried bundles and the terminal table.
// TODO(fri-phase1-proof-thinning): replace these guards with sparse-opening
// checks once `FriRoundWitness` and a slimmer external proof are introduced.
void TestFri3HonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "fri-3 honest prover/verifier passes and round sizes shrink 9 -> 3 -> 1");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{2});
  CHECK_EQ(proof.rounds.size(), std::size_t{3});
  CHECK_EQ(proof.rounds[0].domain_size, std::uint64_t{9});
  CHECK_EQ(proof.rounds[1].domain_size, std::uint64_t{3});
  CHECK_EQ(proof.rounds[2].domain_size, std::uint64_t{1});
  CHECK_EQ(proof.rounds[0].query_positions.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[1].query_positions.size(), std::size_t{1});
  CHECK(proof.rounds[2].query_positions.empty());
  CHECK_EQ(proof.stats.serialized_bytes, CompactFriBytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawFriBytes(ctx, proof));
}

void TestFri3RejectsTamperedOpening() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered queried opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperFirstQueriedOpening(ctx, proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestFri3RejectsTamperedFinalPolynomial() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered terminal polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  ctx.with_ntl_context([&] {
    auto coefficients = proof.final_polynomial.coefficients();
    coefficients[0] += ctx.one();
    proof.final_polynomial = Polynomial(std::move(coefficients));
    return 0;
  });

  CHECK(!verifier.verify(instance, proof));
}

void TestFri3RejectsTamperedTerminalOracleTable() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered terminal oracle table");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto params = MakeParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperTerminalOracleTable(ctx, proof);

  CHECK(!verifier.verify(instance, proof));
}

// TODO(fri-phase1-proof-thinning): keep the schedule and helper coverage below
// when slimming the proof, but rewrite them against the future external proof
// object instead of the current round-witness-shaped struct.
void TestFri9HonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "fri-9 honest prover/verifier passes and round sizes shrink 27 -> 3");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 8);
  const auto params = MakeParams(9, {2});
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.stats.prover_rounds, std::uint64_t{1});
  CHECK_EQ(proof.rounds.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[0].domain_size, std::uint64_t{27});
  CHECK_EQ(proof.rounds[1].domain_size, std::uint64_t{3});
  CHECK_EQ(proof.rounds[0].query_positions.size(), std::size_t{2});
  CHECK(proof.rounds[1].query_positions.empty());
  CHECK_EQ(proof.stats.serialized_bytes, CompactFriBytes(ctx, proof));
  CHECK(proof.stats.serialized_bytes < LegacyRawFriBytes(ctx, proof));
}

void TestFri9RejectsTamperedOpening() {
  testutil::PrintInfo("fri-9 verifier rejects a tampered queried opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const auto instance = MakeInstance(ctx, 27, 8);
  const auto params = MakeParams(9, {2});
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  TamperFirstQueriedOpening(ctx, proof);

  CHECK(!verifier.verify(instance, proof));
}

void TestFriAutoScheduleMatchesConjectureCapacityDefault() {
  testutil::PrintInfo(
      "fri auto query schedule matches the default conjecture-capacity round shape");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx, 9, 8);
  const auto auto_params = MakeAutoParams(3);
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(auto_params);
  const swgr::fri::FriVerifier verifier(auto_params);
  const auto proof = prover.prove(instance, polynomial);

  CHECK(verifier.verify(instance, proof));
  CHECK_EQ(proof.rounds.size(), std::size_t{3});
  CHECK_EQ(proof.rounds[0].query_positions.size(), std::size_t{2});
  CHECK_EQ(proof.rounds[1].query_positions.size(), std::size_t{1});
  CHECK(proof.rounds[2].query_positions.empty());
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
  CHECK_EQ(auto_proof.rounds[0].query_positions.size(), std::size_t{2});
  CHECK_EQ(auto_proof.rounds[1].query_positions.size(), std::size_t{1});
  CHECK_EQ(manual_proof.rounds[0].query_positions.size(), std::size_t{1});
  CHECK_EQ(manual_proof.rounds[1].query_positions.size(), std::size_t{1});
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
  CHECK_EQ(proof.rounds[0].query_positions.size(), std::size_t{3});
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
    RUN_TEST(TestFri3HonestRoundtripAndRoundShape);
    RUN_TEST(TestFri3RejectsTamperedOpening);
    RUN_TEST(TestFri3RejectsTamperedFinalPolynomial);
    RUN_TEST(TestFri3RejectsTamperedTerminalOracleTable);
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

  if (g_failures == 0) {
    std::cout << "\nAll tests passed.\n";
    return 0;
  }

  std::cerr << "\n" << g_failures << " test(s) failed.\n";
  return 1;
}
