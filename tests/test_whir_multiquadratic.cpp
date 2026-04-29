#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "NTL/ZZ_pE.h"

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "tests/test_common.hpp"
#include "whir/multiquadratic.hpp"

using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;
using swgr::whir::MultiQuadraticPolynomial;

int g_failures = 0;

namespace {

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

std::vector<GRElem> SampleCoefficients(const GRContext& ctx,
                                       std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(
        static_cast<std::size_t>(swgr::whir::pow3_checked(variable_count)));
    for (std::uint64_t i = 0; i < swgr::whir::pow3_checked(variable_count);
         ++i) {
      coefficients.push_back(SmallElement((i * 7U + 3U) % 11U));
    }
    return coefficients;
  });
}

void TestBase3HelpersAndPow3() {
  testutil::PrintInfo("base-3 helpers use little-endian variable digits");

  CHECK_EQ(swgr::whir::pow3_checked(0), std::uint64_t{1});
  CHECK_EQ(swgr::whir::pow3_checked(5), std::uint64_t{243});

  const std::vector<std::uint8_t> digits{2, 0, 1, 2};
  const std::uint64_t index = swgr::whir::encode_base3_index(digits);
  CHECK_EQ(index, std::uint64_t{65});

  const auto decoded = swgr::whir::decode_base3_index(index, 4);
  CHECK_EQ(decoded.size(), digits.size());
  for (std::size_t i = 0; i < digits.size(); ++i) {
    CHECK_EQ(decoded[i], digits[i]);
  }
}

void TestPowM() {
  testutil::PrintInfo("Pow_m returns x, x^3, x^9, ... under the GR context");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const GRElem x = ctx.with_ntl_context([&] { return SmallElement(5); });
  const auto powers = swgr::whir::pow_m(ctx, x, 4);

  CHECK_EQ(powers.size(), std::size_t{4});
  ctx.with_ntl_context([&] {
    CHECK_EQ(powers[0], x);
    CHECK_EQ(powers[1], NTL::power(x, 3L));
    CHECK_EQ(powers[2], NTL::power(x, 9L));
    CHECK_EQ(powers[3], NTL::power(x, 27L));
    return 0;
  });
}

void TestEvaluatePowMatchesUnivariate() {
  testutil::PrintInfo(
      "evaluate_pow matches the univariate Pow_m polynomial encoding");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const MultiQuadraticPolynomial poly(3, SampleCoefficients(ctx, 3));
  const auto x = ctx.with_ntl_context([&] { return SmallElement(7); });

  const auto univariate = poly.to_univariate_pow_polynomial(ctx);
  CHECK_EQ(poly.evaluate_pow(ctx, x), univariate.evaluate(ctx, x));

  const auto pow_point = swgr::whir::pow_m(ctx, x, poly.variable_count());
  CHECK_EQ(poly.evaluate(ctx, pow_point), poly.evaluate_pow(ctx, x));
}

void TestRestrictPrefixMatchesOriginalEvaluation() {
  testutil::PrintInfo(
      "prefix restriction evaluates like fixing the original variables");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const MultiQuadraticPolynomial poly(3, SampleCoefficients(ctx, 3));

  const auto [alphas, tail] = ctx.with_ntl_context([&] {
    std::vector<GRElem> fixed{SmallElement(2), SmallElement(5)};
    std::vector<GRElem> rest{SmallElement(9)};
    return std::pair(std::move(fixed), std::move(rest));
  });

  const MultiQuadraticPolynomial restricted = poly.restrict_prefix(ctx, alphas);
  CHECK_EQ(restricted.variable_count(), std::uint64_t{1});

  std::vector<GRElem> full_point = alphas;
  full_point.insert(full_point.end(), tail.begin(), tail.end());
  CHECK_EQ(restricted.evaluate(ctx, tail), poly.evaluate(ctx, full_point));
}

void TestMultilinearEmbedding() {
  testutil::PrintInfo(
      "zero quadratic coefficients embed multilinear polynomials");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto coefficients = ctx.with_ntl_context([&] {
    std::vector<GRElem> coeffs(9);
    for (auto& coefficient : coeffs) {
      NTL::clear(coefficient);
    }
    coeffs[0] = SmallElement(2);
    coeffs[1] = SmallElement(3);
    coeffs[3] = SmallElement(5);
    coeffs[4] = SmallElement(7);
    return coeffs;
  });

  const MultiQuadraticPolynomial poly(2, coefficients);
  const auto point = ctx.with_ntl_context([&] {
    const swgr::Domain grid = swgr::Domain::teichmuller_subgroup(ctx, 3);
    return std::vector<GRElem>{grid.element(1), grid.element(2)};
  });

  const GRElem expected = ctx.with_ntl_context([&] {
    return coefficients[0] + coefficients[1] * point[0] +
           coefficients[3] * point[1] + coefficients[4] * point[0] * point[1];
  });
  CHECK_EQ(poly.evaluate(ctx, point), expected);
}

void TestInvalidInputsReject() {
  testutil::PrintInfo(
      "invalid coefficient lengths, arities, and overflow reject");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto too_many_coefficients =
      ctx.with_ntl_context([&] { return std::vector<GRElem>(10, ctx.one()); });

  bool bad_length_threw = false;
  try {
    (void)MultiQuadraticPolynomial(2, too_many_coefficients);
  } catch (const std::invalid_argument&) {
    bad_length_threw = true;
  }
  CHECK(bad_length_threw);

  const MultiQuadraticPolynomial poly(2, SampleCoefficients(ctx, 2));
  bool bad_point_threw = false;
  try {
    const auto point =
        ctx.with_ntl_context([&] { return std::vector<GRElem>{ctx.one()}; });
    (void)poly.evaluate(ctx, point);
  } catch (const std::invalid_argument&) {
    bad_point_threw = true;
  }
  CHECK(bad_point_threw);

  bool bad_restriction_threw = false;
  try {
    const auto alphas = ctx.with_ntl_context([&] {
      return std::vector<GRElem>{ctx.one(), ctx.one(), ctx.one()};
    });
    (void)poly.restrict_prefix(ctx, alphas);
  } catch (const std::invalid_argument&) {
    bad_restriction_threw = true;
  }
  CHECK(bad_restriction_threw);

  bool bad_digit_threw = false;
  try {
    const std::vector<std::uint8_t> digits{0, 3};
    (void)swgr::whir::encode_base3_index(digits);
  } catch (const std::invalid_argument&) {
    bad_digit_threw = true;
  }
  CHECK(bad_digit_threw);

  bool bad_decode_threw = false;
  try {
    (void)swgr::whir::decode_base3_index(9, 2);
  } catch (const std::out_of_range&) {
    bad_decode_threw = true;
  }
  CHECK(bad_decode_threw);

  bool overflow_threw = false;
  try {
    (void)swgr::whir::pow3_checked(41);
  } catch (const std::overflow_error&) {
    overflow_threw = true;
  }
  CHECK(overflow_threw);
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestBase3HelpersAndPow3);
    RUN_TEST(TestPowM);
    RUN_TEST(TestEvaluatePowMatchesUnivariate);
    RUN_TEST(TestRestrictPrefixMatchesOriginalEvaluation);
    RUN_TEST(TestMultilinearEmbedding);
    RUN_TEST(TestInvalidInputsReject);
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
