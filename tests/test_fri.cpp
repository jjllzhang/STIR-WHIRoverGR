#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "fri/proof_size_estimator.hpp"
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

swgr::fri::FriParameters MakeParams() {
  swgr::fri::FriParameters params;
  params.fold_factor = 3;
  params.stop_degree = 1;
  params.query_repetitions = {2, 1};
  params.lambda_target = 64;
  params.pow_bits = 0;
  params.sec_mode = swgr::SecurityMode::ConjectureCapacity;
  params.hash_profile = swgr::HashProfile::STIR_NATIVE;
  return params;
}

swgr::fri::FriInstance MakeInstance(const GRContext& ctx) {
  return swgr::fri::FriInstance{
      .domain = Domain::teichmuller_subgroup(ctx, 9),
      .claimed_degree = 8,
  };
}

void TestFri3HonestRoundtripAndRoundShape() {
  testutil::PrintInfo(
      "fri-3 honest prover/verifier passes and round sizes shrink 9 -> 3 -> 1");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
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
}

void TestFri3RejectsTamperedOpening() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered queried opening");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
  const auto polynomial = SamplePolynomial(ctx, instance.domain, 9);

  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);
  auto proof = prover.prove(instance, polynomial);

  const std::uint64_t index = proof.rounds[0].query_positions.front();
  ctx.with_ntl_context([&] {
    proof.rounds[0].oracle_evals[static_cast<std::size_t>(index)] += ctx.one();
    return 0;
  });

  CHECK(!verifier.verify(instance, proof));
}

void TestFri3RejectsTamperedFinalPolynomial() {
  testutil::PrintInfo("fri-3 verifier rejects a tampered terminal polynomial");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx);
  const auto params = MakeParams();
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

void TestFri3EstimatorProducesStructuredOutput() {
  testutil::PrintInfo("fri-3 estimator returns non-zero bytes, hashes, and json");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto instance = MakeInstance(ctx);
  const swgr::fri::FriProofSizeEstimator estimator(MakeParams());
  const auto estimate = estimator.estimate(instance);

  CHECK(estimate.argument_bytes > 0);
  CHECK(estimate.verifier_hashes > 0);
  CHECK(estimate.round_breakdown_json.find("\"rounds\"") != std::string::npos);
  CHECK(estimate.round_breakdown_json.find("\"final_polynomial_bytes\"") !=
        std::string::npos);
}

void TestFriValidationRejectsBadInputs() {
  testutil::PrintInfo("fri parameter validation rejects zero queries and bad degree");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  auto params = MakeParams();
  params.query_repetitions = {2, 0};
  CHECK(!swgr::fri::validate(params));

  const auto instance = MakeInstance(ctx);
  CHECK(swgr::fri::validate(MakeParams(), instance));

  const swgr::fri::FriInstance bad_instance{
      .domain = instance.domain,
      .claimed_degree = instance.domain.size(),
  };
  CHECK(!swgr::fri::validate(MakeParams(), bad_instance));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestFri3HonestRoundtripAndRoundShape);
    RUN_TEST(TestFri3RejectsTamperedOpening);
    RUN_TEST(TestFri3RejectsTamperedFinalPolynomial);
    RUN_TEST(TestFri3EstimatorProducesStructuredOutput);
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
