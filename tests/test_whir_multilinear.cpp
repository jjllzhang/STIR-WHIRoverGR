#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "NTL/ZZ_pE.h"

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "tests/test_common.hpp"
#include "whir/common.hpp"
#include "whir/multilinear.hpp"
#include "whir/prover.hpp"
#include "whir/verifier.hpp"

int g_failures = 0;

namespace {

using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;
using stir_whir_gr::algebra::GRElem;

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

std::vector<GRElem> SmallCoefficients(const GRContext& ctx,
                                      std::uint64_t count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
      coefficients.push_back(SmallElement(3U + 2U * i));
    }
    return coefficients;
  });
}

stir_whir_gr::whir::WhirPublicParameters BuildPublicParameters() {
  constexpr std::uint64_t kVariableCount = 2;
  constexpr std::uint64_t kDomainSize = 27;
  auto ctx = std::make_shared<GRContext>(
      GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const stir_whir_gr::Domain domain =
      stir_whir_gr::Domain::teichmuller_subgroup(ctx, kDomainSize);
  return ctx->with_ntl_context([&] {
    const GRElem omega =
        NTL::power(domain.root(), static_cast<long>(kDomainSize / 3U));
    return stir_whir_gr::whir::WhirPublicParameters{
        .ctx = ctx,
        .initial_domain = domain,
        .variable_count = kVariableCount,
        .layer_widths = {1, 1},
        .shift_repetitions = {1, 1},
        .final_repetitions = 1,
        .degree_bounds = {4, 4},
        .deltas = {0.1L, 0.1L},
        .omega = omega,
        .ternary_grid = {ctx->one(), omega, omega * omega},
        .lambda_target = 32,
        .hash_profile = stir_whir_gr::HashProfile::WHIR_NATIVE,
    };
  });
}

void TestEvaluationAndEmbedding() {
  testutil::PrintInfo(
      "WHIR multilinear polynomial embeds into the ternary coefficient basis");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto coefficients = SmallCoefficients(ctx, 4);
  const stir_whir_gr::whir::MultilinearPolynomial multilinear(2, coefficients);
  const auto embedded = multilinear.to_multi_quadratic(ctx);

  CHECK_EQ(stir_whir_gr::whir::pow2_checked(4), std::uint64_t{16});
  CHECK_EQ(embedded.variable_count(), std::uint64_t{2});
  CHECK_EQ(embedded.coefficients().size(), std::size_t{5});
  CHECK_EQ(embedded.coefficients()[0], coefficients[0]);
  CHECK_EQ(embedded.coefficients()[1], coefficients[1]);
  CHECK_EQ(embedded.coefficients()[2], ctx.zero());
  CHECK_EQ(embedded.coefficients()[3], coefficients[2]);
  CHECK_EQ(embedded.coefficients()[4], coefficients[3]);

  const auto point = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{SmallElement(5), SmallElement(7)};
  });
  CHECK_EQ(multilinear.evaluate(ctx, point), embedded.evaluate(ctx, point));

  const auto x = ctx.with_ntl_context([&] { return SmallElement(11); });
  CHECK_EQ(multilinear.evaluate_pow(ctx, x), embedded.evaluate_pow(ctx, x));
}

void TestInvalidInputsReject() {
  testutil::PrintInfo(
      "WHIR multilinear polynomial rejects invalid lengths and arities");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  bool bad_length_threw = false;
  try {
    (void)stir_whir_gr::whir::MultilinearPolynomial(2, SmallCoefficients(ctx, 5));
  } catch (const std::invalid_argument&) {
    bad_length_threw = true;
  }
  CHECK(bad_length_threw);

  const stir_whir_gr::whir::MultilinearPolynomial polynomial(2,
                                                    SmallCoefficients(ctx, 4));
  bool bad_point_threw = false;
  try {
    (void)polynomial.evaluate(ctx, SmallCoefficients(ctx, 1));
  } catch (const std::invalid_argument&) {
    bad_point_threw = true;
  }
  CHECK(bad_point_threw);

  bool overflow_threw = false;
  try {
    (void)stir_whir_gr::whir::pow2_checked(64);
  } catch (const std::overflow_error&) {
    overflow_threw = true;
  }
  CHECK(overflow_threw);
}

void TestWhirRoundtripAcceptsMultilinearPolynomial() {
  testutil::PrintInfo(
      "WHIR commit/open/verify accepts a multilinear polynomial");

  const auto pp = BuildPublicParameters();
  stir_whir_gr::whir::WhirParameters params;
  params.lambda_target = pp.lambda_target;
  params.hash_profile = pp.hash_profile;

  const stir_whir_gr::whir::MultilinearPolynomial polynomial(
      2, SmallCoefficients(*pp.ctx, 4));
  const auto point = pp.ctx->with_ntl_context([&] {
    return std::vector<GRElem>{SmallElement(13), SmallElement(17)};
  });

  const stir_whir_gr::whir::WhirProver prover(params);
  const stir_whir_gr::whir::WhirVerifier verifier(params);
  stir_whir_gr::whir::WhirCommitmentState state;
  const auto commitment = prover.commit(pp, polynomial, &state);
  const auto opening = prover.open(commitment, state, point);

  CHECK(verifier.verify(commitment, point, opening));
  CHECK_EQ(opening.value, polynomial.evaluate(*pp.ctx, point));
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestEvaluationAndEmbedding);
    RUN_TEST(TestInvalidInputsReject);
    RUN_TEST(TestWhirRoundtripAcceptsMultilinearPolynomial);
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled std::exception: " << ex.what() << "\n";
    return 2;
  } catch (...) {
    std::cerr << "Unhandled non-std exception\n";
    return 2;
  }

  if (g_failures == 0) {
    std::cout << "All WHIR multilinear tests passed\n";
  }
  return g_failures == 0 ? 0 : 1;
}
