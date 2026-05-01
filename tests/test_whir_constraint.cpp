#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

#include "NTL/ZZ_pE.h"
#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "tests/test_common.hpp"
#include "whir/constraint.hpp"
#include "whir/multiquadratic.hpp"

using stir_whir_gr::Domain;
using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;
using stir_whir_gr::algebra::GRElem;
using stir_whir_gr::whir::MultiQuadraticPolynomial;
using stir_whir_gr::whir::MultilinearPolynomial;
using stir_whir_gr::whir::TernaryGrid;
using stir_whir_gr::whir::WhirConstraint;
using stir_whir_gr::whir::WhirSumcheckPolynomial;

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

GRElem SmallElement(const GRContext &ctx, std::uint64_t value) {
  return ctx.with_ntl_context([&] { return SmallElement(value); });
}

TernaryGrid MakeGrid(const GRContext &ctx) {
  const Domain subgroup = Domain::teichmuller_subgroup(ctx, 3);
  return stir_whir_gr::whir::ternary_grid(ctx, subgroup.root());
}

std::vector<GRElem> SampleCoefficients(const GRContext &ctx,
                                       std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(
        static_cast<std::size_t>(stir_whir_gr::whir::pow3_checked(variable_count)));
    for (std::uint64_t i = 0; i < stir_whir_gr::whir::pow3_checked(variable_count);
         ++i) {
      coefficients.push_back(SmallElement((5U * i + 2U) % 13U));
    }
    return coefficients;
  });
}

std::vector<GRElem> SamplePoint(const GRContext &ctx, const TernaryGrid &grid,
                                std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> point;
    point.reserve(static_cast<std::size_t>(variable_count));
    for (std::uint64_t i = 0; i < variable_count; ++i) {
      point.push_back(grid[static_cast<std::size_t>((i + 1U) % 3U)] +
                      SmallElement(i + 2U));
    }
    return point;
  });
}

std::vector<GRElem> SampleGridPrefix(const TernaryGrid &grid,
                                     std::uint64_t prefix_length) {
  std::vector<GRElem> prefix;
  prefix.reserve(static_cast<std::size_t>(prefix_length));
  for (std::uint64_t i = 0; i < prefix_length; ++i) {
    prefix.push_back(grid[static_cast<std::size_t>((i + 2U) % 3U)]);
  }
  return prefix;
}

std::vector<GRElem> SampleOffGridPrefix(const GRContext &ctx,
                                        const TernaryGrid &grid,
                                        std::uint64_t prefix_length) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> prefix;
    prefix.reserve(static_cast<std::size_t>(prefix_length));
    for (std::uint64_t i = 0; i < prefix_length; ++i) {
      prefix.push_back(grid[static_cast<std::size_t>((i + 1U) % 3U)] +
                       SmallElement(i + 5U));
    }
    return prefix;
  });
}

std::vector<GRElem> SparseHighCoefficients(const GRContext &ctx,
                                           std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients(static_cast<std::size_t>(
        stir_whir_gr::whir::pow3_checked(variable_count)));
    for (auto &coefficient : coefficients) {
      NTL::clear(coefficient);
    }
    if (!coefficients.empty()) {
      coefficients.front() = SmallElement(3);
      coefficients.back() = SmallElement(11);
    }
    std::uint64_t mixed_index = 0;
    std::uint64_t place = 1;
    for (std::uint64_t i = 0; i < variable_count; ++i) {
      mixed_index += ((i % 3U) == 0U ? 2U : 1U) * place;
      place *= 3U;
    }
    if (mixed_index < coefficients.size()) {
      coefficients[static_cast<std::size_t>(mixed_index)] += SmallElement(7);
    }
    return coefficients;
  });
}

std::vector<GRElem> EmbeddedMultilinearCoefficients(
    const GRContext &ctx, std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(static_cast<std::size_t>(
        stir_whir_gr::whir::pow2_checked(variable_count)));
    for (std::uint64_t i = 0; i < stir_whir_gr::whir::pow2_checked(variable_count);
         ++i) {
      coefficients.push_back(SmallElement((7U * i + 4U) % 17U));
    }
    return coefficients;
  });
}

std::vector<GRElem> TrimmedHighZeroCoefficients(const GRContext &ctx,
                                                std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients(static_cast<std::size_t>(
        stir_whir_gr::whir::pow3_checked(variable_count)));
    for (auto &coefficient : coefficients) {
      NTL::clear(coefficient);
    }
    if (!coefficients.empty()) {
      coefficients.front() = SmallElement(5);
    }
    if (coefficients.size() > 4U) {
      coefficients[3] = SmallElement(9);
    }
    return coefficients;
  });
}

std::vector<MultiQuadraticPolynomial> DifferentialPolynomials(
    const GRContext &ctx, std::uint64_t variable_count) {
  std::vector<MultiQuadraticPolynomial> polynomials;
  polynomials.emplace_back(variable_count, SampleCoefficients(ctx, variable_count));
  polynomials.emplace_back(variable_count,
                           SparseHighCoefficients(ctx, variable_count));
  polynomials.push_back(
      MultilinearPolynomial(variable_count,
                            EmbeddedMultilinearCoefficients(ctx, variable_count))
          .to_multi_quadratic(ctx));
  polynomials.emplace_back(variable_count, std::vector<GRElem>{});
  polynomials.emplace_back(variable_count,
                           TrimmedHighZeroCoefficients(ctx, variable_count));
  return polynomials;
}

WhirConstraint MakeShiftLikeConstraint(const GRContext &ctx,
                                       const TernaryGrid &grid,
                                       std::uint64_t variable_count) {
  WhirConstraint constraint(grid);
  constraint.add_shift_term(ctx.one(), SamplePoint(ctx, grid, variable_count));
  ctx.with_ntl_context([&] {
    const GRElem first_weight = grid[2] + SmallElement(3);
    const GRElem pow_base = grid[1] + SmallElement(4);
    constraint.add_shift_term(first_weight,
                              stir_whir_gr::whir::pow_m(ctx, pow_base,
                                                         variable_count));
    const GRElem second_weight = grid[1] + SmallElement(6);
    constraint.add_shift_term(second_weight,
                              SampleOffGridPrefix(ctx, grid, variable_count));
    return 0;
  });
  return constraint;
}

std::vector<WhirConstraint> DifferentialConstraints(const GRContext &ctx,
                                                    const TernaryGrid &grid,
                                                    std::uint64_t variable_count) {
  std::vector<WhirConstraint> constraints;
  constraints.emplace_back(grid);

  WhirConstraint single_term(grid);
  single_term.add_shift_term(ctx.one(), SamplePoint(ctx, grid, variable_count));
  constraints.push_back(std::move(single_term));
  constraints.push_back(MakeShiftLikeConstraint(ctx, grid, variable_count));
  return constraints;
}

WhirSumcheckPolynomial GenericReferenceSumcheck(
    const GRContext &ctx, const MultiQuadraticPolynomial &polynomial,
    const WhirConstraint &constraint, std::span<const GRElem> prefix) {
  return stir_whir_gr::whir::honest_sumcheck_polynomial(
      ctx, polynomial.variable_count(), constraint, prefix,
      [&](std::span<const GRElem> point) {
        return polynomial.evaluate(ctx, point);
      });
}

void CheckSumcheckPolynomialsEqual(const GRContext &ctx, const TernaryGrid &grid,
                                   const WhirSumcheckPolynomial &actual,
                                   const WhirSumcheckPolynomial &expected) {
  CHECK_EQ(actual.coefficients.size(), expected.coefficients.size());
  const std::size_t shared_size =
      std::min(actual.coefficients.size(), expected.coefficients.size());
  for (std::size_t i = 0; i < shared_size; ++i) {
    CHECK_EQ(actual.coefficients[i], expected.coefficients[i]);
  }

  ctx.with_ntl_context([&] {
    std::vector<GRElem> evaluation_points{grid[0], grid[1], grid[2]};
    const auto interpolation_points =
        stir_whir_gr::whir::sumcheck_interpolation_points(ctx);
    evaluation_points.insert(evaluation_points.end(), interpolation_points.begin(),
                             interpolation_points.end());
    evaluation_points.push_back(grid[1] + SmallElement(9));
    evaluation_points.push_back(ctx.teich_generator() + SmallElement(2));
    for (const auto &point : evaluation_points) {
      CHECK_EQ(stir_whir_gr::whir::evaluate_sumcheck_polynomial(ctx, actual, point),
               stir_whir_gr::whir::evaluate_sumcheck_polynomial(ctx, expected,
                                                                 point));
    }
    return 0;
  });
}

std::vector<GRElem> GridPointFromIndex(const TernaryGrid &grid,
                                       std::uint64_t variable_count,
                                       std::uint64_t index) {
  std::vector<GRElem> point;
  point.reserve(static_cast<std::size_t>(variable_count));
  for (std::uint64_t i = 0; i < variable_count; ++i) {
    point.push_back(grid[static_cast<std::size_t>(index % 3U)]);
    index /= 3U;
  }
  return point;
}

void TestCoefficientIndexSplitMatchesBase3Digits() {
  testutil::PrintInfo(
      "sumcheck prefix/live/tail index split matches little-endian base-3");

  for (std::uint64_t m = 1; m <= 6; ++m) {
    const std::uint64_t bound = stir_whir_gr::whir::pow3_checked(m);
    const std::vector<std::uint64_t> indices{
        0,
        1,
        bound / 2U,
        bound - 1U,
    };
    for (std::uint64_t prefix_length = 0; prefix_length < m; ++prefix_length) {
      const std::uint64_t prefix_size =
          stir_whir_gr::whir::pow3_checked(prefix_length);
      const std::uint64_t tail_stride = prefix_size * 3U;
      for (const std::uint64_t index : indices) {
        const auto digits = stir_whir_gr::whir::decode_base3_index(index, m);
        const std::uint64_t prefix_index = index % prefix_size;
        const std::uint8_t live_digit =
            static_cast<std::uint8_t>((index / prefix_size) % 3U);
        const std::uint64_t tail_index = index / tail_stride;

        std::uint64_t reconstructed_prefix = 0;
        std::uint64_t place = 1;
        for (std::uint64_t i = 0; i < prefix_length; ++i) {
          reconstructed_prefix += digits[static_cast<std::size_t>(i)] * place;
          place *= 3U;
        }
        CHECK_EQ(prefix_index, reconstructed_prefix);
        CHECK_EQ(live_digit, digits[static_cast<std::size_t>(prefix_length)]);

        std::uint64_t reconstructed_tail = 0;
        place = 1;
        for (std::uint64_t i = prefix_length + 1U; i < m; ++i) {
          reconstructed_tail += digits[static_cast<std::size_t>(i)] * place;
          place *= 3U;
        }
        CHECK_EQ(tail_index, reconstructed_tail);
      }
    }
  }
}

GRElem SumOverGrid(const GRContext &ctx, const TernaryGrid &grid,
                   const MultiQuadraticPolynomial &polynomial,
                   const WhirConstraint &constraint) {
  return ctx.with_ntl_context([&] {
    GRElem sum;
    NTL::clear(sum);
    for (std::uint64_t i = 0;
         i < stir_whir_gr::whir::pow3_checked(polynomial.variable_count()); ++i) {
      const auto point =
          GridPointFromIndex(grid, polynomial.variable_count(), i);
      sum +=
          polynomial.evaluate(ctx, point) * constraint.evaluate_A(ctx, point);
    }
    return sum;
  });
}

void TestTernaryGridAndLagrangeBasis() {
  testutil::PrintInfo(
      "ternary grid has unit differences and Kronecker Lagrange basis");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const TernaryGrid grid = MakeGrid(ctx);

  CHECK(stir_whir_gr::whir::points_have_pairwise_unit_differences(ctx, grid));

  ctx.with_ntl_context([&] {
    for (std::size_t i = 0; i < grid.size(); ++i) {
      for (std::size_t j = 0; j < grid.size(); ++j) {
        const GRElem actual =
            stir_whir_gr::whir::lagrange_basis_on_ternary_grid(ctx, grid, i, grid[j]);
        CHECK_EQ(actual, i == j ? ctx.one() : ctx.zero());
      }
    }

    const auto interpolation_points =
        stir_whir_gr::whir::sumcheck_interpolation_points(ctx);
    CHECK_EQ(interpolation_points.size(), std::size_t{5});
    CHECK(stir_whir_gr::whir::points_have_pairwise_unit_differences(
        ctx, interpolation_points));
    return 0;
  });
}

void TestEqualityKernelReproducesMultiQuadratic() {
  testutil::PrintInfo(
      "sum over B^m against eq_B(z, X) reproduces F(z) for m=1,2,3");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const TernaryGrid grid = MakeGrid(ctx);

  for (std::uint64_t m = 1; m <= 3; ++m) {
    const MultiQuadraticPolynomial polynomial(m, SampleCoefficients(ctx, m));
    const auto z = SamplePoint(ctx, grid, m);

    WhirConstraint constraint(grid);
    constraint.add_shift_term(ctx.one(), z);

    CHECK_EQ(SumOverGrid(ctx, grid, polynomial, constraint),
             polynomial.evaluate(ctx, z));
  }
}

void TestConstraintRestrictionMatchesDirectEvaluation() {
  testutil::PrintInfo(
      "constraint prefix restriction matches direct full-point evaluation");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const TernaryGrid grid = MakeGrid(ctx);
  const auto first_point = SamplePoint(ctx, grid, 3);
  const auto second_point = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{grid[2], grid[0] + SmallElement(3), grid[1]};
  });
  const auto prefix = ctx.with_ntl_context(
      [&] { return std::vector<GRElem>{grid[1] + SmallElement(5)}; });
  const auto tail = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{grid[0] + SmallElement(7), grid[2]};
  });

  WhirConstraint constraint(grid);
  constraint.add_shift_term(SmallElement(ctx, 2), first_point);
  const GRElem second_weight =
      ctx.with_ntl_context([&] { return grid[1] + SmallElement(1); });
  constraint.add_shift_term(second_weight, second_point);

  WhirConstraint restricted = constraint.restrict_prefix(ctx, prefix);
  CHECK_EQ(restricted.variable_count(), std::uint64_t{2});

  std::vector<GRElem> full_point = prefix;
  full_point.insert(full_point.end(), tail.begin(), tail.end());
  CHECK_EQ(restricted.evaluate_A(ctx, tail),
           constraint.evaluate_A(ctx, full_point));
}

void TestHonestSumcheckIdentities() {
  testutil::PrintInfo(
      "honest one-variable sumcheck polynomials satisfy verifier identities");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const TernaryGrid grid = MakeGrid(ctx);

  for (std::uint64_t m = 1; m <= 3; ++m) {
    const MultiQuadraticPolynomial polynomial(m, SampleCoefficients(ctx, m));
    const auto z = SamplePoint(ctx, grid, m);

    WhirConstraint constraint(grid);
    constraint.add_shift_term(ctx.one(), z);

    GRElem current_sigma = polynomial.evaluate(ctx, z);
    std::vector<GRElem> prefix;
    for (std::uint64_t round = 0; round < m; ++round) {
      const WhirSumcheckPolynomial h = stir_whir_gr::whir::honest_sumcheck_polynomial(
          ctx, polynomial, constraint, prefix);
      CHECK(stir_whir_gr::whir::check_sumcheck_degree(h, 4));
      CHECK(
          stir_whir_gr::whir::check_sumcheck_identity(ctx, grid, h, current_sigma, 4));

      const GRElem alpha = ctx.with_ntl_context([&] {
        return grid[static_cast<std::size_t>(round % 3U)] +
               SmallElement(round + 4U);
      });
      current_sigma = stir_whir_gr::whir::sumcheck_next_sigma(ctx, h, alpha);
      prefix.push_back(alpha);
    }
  }
}

void TestMultiQuadraticSumcheckMatchesGenericReference() {
  testutil::PrintInfo(
      "multi-quadratic sumcheck fast path matches generic enumerative oracle");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 12});
  const TernaryGrid grid = MakeGrid(ctx);

  for (std::uint64_t m = 1; m <= 4; ++m) {
    const auto polynomials = DifferentialPolynomials(ctx, m);
    const auto constraints = DifferentialConstraints(ctx, grid, m);
    for (const auto &polynomial : polynomials) {
      for (const auto &constraint : constraints) {
        for (std::uint64_t prefix_length = 0; prefix_length < m; ++prefix_length) {
          const std::vector<std::vector<GRElem>> prefixes{
              SampleGridPrefix(grid, prefix_length),
              SampleOffGridPrefix(ctx, grid, prefix_length),
          };
          for (const auto &prefix : prefixes) {
            const WhirSumcheckPolynomial actual =
                stir_whir_gr::whir::honest_sumcheck_polynomial(
                    ctx, polynomial, constraint, prefix);
            const WhirSumcheckPolynomial expected =
                GenericReferenceSumcheck(ctx, polynomial, constraint, prefix);
            CheckSumcheckPolynomialsEqual(ctx, grid, actual, expected);
          }
        }
      }
    }
  }
}

void TestTamperingAndDeclaredDegreeFail() {
  testutil::PrintInfo(
      "tampered coefficients and oversized declared degree "
      "fail verifier helpers");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const TernaryGrid grid = MakeGrid(ctx);
  const MultiQuadraticPolynomial polynomial(2, SampleCoefficients(ctx, 2));
  const auto z = SamplePoint(ctx, grid, 2);

  WhirConstraint constraint(grid);
  constraint.add_shift_term(ctx.one(), z);

  const WhirSumcheckPolynomial honest =
      stir_whir_gr::whir::honest_sumcheck_polynomial(ctx, polynomial, constraint, {});
  const GRElem current_sigma = polynomial.evaluate(ctx, z);
  CHECK(
      stir_whir_gr::whir::check_sumcheck_identity(ctx, grid, honest, current_sigma, 4));

  WhirSumcheckPolynomial tampered = honest;
  if (tampered.coefficients.empty()) {
    tampered.coefficients.push_back(ctx.one());
  } else {
    ctx.with_ntl_context([&] {
      tampered.coefficients.front() += ctx.one();
      return 0;
    });
  }
  CHECK(!stir_whir_gr::whir::check_sumcheck_identity(ctx, grid, tampered, current_sigma,
                                             4));

  WhirSumcheckPolynomial declared_too_large = honest;
  declared_too_large.coefficients.resize(6, ctx.zero());
  CHECK(!stir_whir_gr::whir::check_sumcheck_degree(declared_too_large, 4));
  CHECK(!stir_whir_gr::whir::check_sumcheck_identity(ctx, grid, declared_too_large,
                                             current_sigma, 4));
}

void TestInvalidShapesReject() {
  testutil::PrintInfo("constraint arity and live-variable shape checks reject");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const TernaryGrid grid = MakeGrid(ctx);

  WhirConstraint constraint(grid);
  constraint.add_shift_term(ctx.one(), SamplePoint(ctx, grid, 2));

  bool bad_term_threw = false;
  try {
    constraint.add_shift_term(ctx.one(), SamplePoint(ctx, grid, 3));
  } catch (const std::invalid_argument &) {
    bad_term_threw = true;
  }
  CHECK(bad_term_threw);

  const MultiQuadraticPolynomial polynomial(2, SampleCoefficients(ctx, 2));
  bool no_live_variable_threw = false;
  try {
    const auto full_prefix = SamplePoint(ctx, grid, 2);
    (void)stir_whir_gr::whir::honest_sumcheck_polynomial(ctx, polynomial, constraint,
                                                 full_prefix);
  } catch (const std::invalid_argument &) {
    no_live_variable_threw = true;
  }
  CHECK(no_live_variable_threw);
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestTernaryGridAndLagrangeBasis);
    RUN_TEST(TestCoefficientIndexSplitMatchesBase3Digits);
    RUN_TEST(TestEqualityKernelReproducesMultiQuadratic);
    RUN_TEST(TestConstraintRestrictionMatchesDirectEvaluation);
    RUN_TEST(TestHonestSumcheckIdentities);
    RUN_TEST(TestMultiQuadraticSumcheckMatchesGenericReference);
    RUN_TEST(TestTamperingAndDeclaredDegreeFail);
    RUN_TEST(TestInvalidShapesReject);
  } catch (const std::exception &ex) {
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
