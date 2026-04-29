#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "NTL/ZZ_pE.h"
#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "tests/test_common.hpp"
#include "whir/constraint.hpp"
#include "whir/multiquadratic.hpp"

using swgr::Domain;
using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;
using swgr::whir::MultiQuadraticPolynomial;
using swgr::whir::TernaryGrid;
using swgr::whir::WhirConstraint;
using swgr::whir::WhirSumcheckPolynomial;

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
  return swgr::whir::ternary_grid(ctx, subgroup.root());
}

std::vector<GRElem> SampleCoefficients(const GRContext &ctx,
                                       std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(
        static_cast<std::size_t>(swgr::whir::pow3_checked(variable_count)));
    for (std::uint64_t i = 0; i < swgr::whir::pow3_checked(variable_count);
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

GRElem SumOverGrid(const GRContext &ctx, const TernaryGrid &grid,
                   const MultiQuadraticPolynomial &polynomial,
                   const WhirConstraint &constraint) {
  return ctx.with_ntl_context([&] {
    GRElem sum;
    NTL::clear(sum);
    for (std::uint64_t i = 0;
         i < swgr::whir::pow3_checked(polynomial.variable_count()); ++i) {
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

  CHECK(swgr::whir::points_have_pairwise_unit_differences(ctx, grid));

  ctx.with_ntl_context([&] {
    for (std::size_t i = 0; i < grid.size(); ++i) {
      for (std::size_t j = 0; j < grid.size(); ++j) {
        const GRElem actual =
            swgr::whir::lagrange_basis_on_ternary_grid(ctx, grid, i, grid[j]);
        CHECK_EQ(actual, i == j ? ctx.one() : ctx.zero());
      }
    }

    const auto interpolation_points =
        swgr::whir::sumcheck_interpolation_points(ctx);
    CHECK_EQ(interpolation_points.size(), std::size_t{5});
    CHECK(swgr::whir::points_have_pairwise_unit_differences(
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
      const WhirSumcheckPolynomial h = swgr::whir::honest_sumcheck_polynomial(
          ctx, polynomial, constraint, prefix);
      CHECK(swgr::whir::check_sumcheck_degree(h, 4));
      CHECK(
          swgr::whir::check_sumcheck_identity(ctx, grid, h, current_sigma, 4));

      const GRElem alpha = ctx.with_ntl_context([&] {
        return grid[static_cast<std::size_t>(round % 3U)] +
               SmallElement(round + 4U);
      });
      current_sigma = swgr::whir::sumcheck_next_sigma(ctx, h, alpha);
      prefix.push_back(alpha);
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
      swgr::whir::honest_sumcheck_polynomial(ctx, polynomial, constraint, {});
  const GRElem current_sigma = polynomial.evaluate(ctx, z);
  CHECK(
      swgr::whir::check_sumcheck_identity(ctx, grid, honest, current_sigma, 4));

  WhirSumcheckPolynomial tampered = honest;
  if (tampered.coefficients.empty()) {
    tampered.coefficients.push_back(ctx.one());
  } else {
    ctx.with_ntl_context([&] {
      tampered.coefficients.front() += ctx.one();
      return 0;
    });
  }
  CHECK(!swgr::whir::check_sumcheck_identity(ctx, grid, tampered, current_sigma,
                                             4));

  WhirSumcheckPolynomial declared_too_large = honest;
  declared_too_large.coefficients.resize(6, ctx.zero());
  CHECK(!swgr::whir::check_sumcheck_degree(declared_too_large, 4));
  CHECK(!swgr::whir::check_sumcheck_identity(ctx, grid, declared_too_large,
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
    (void)swgr::whir::honest_sumcheck_polynomial(ctx, polynomial, constraint,
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
    RUN_TEST(TestEqualityKernelReproducesMultiQuadratic);
    RUN_TEST(TestConstraintRestrictionMatchesDirectEvaluation);
    RUN_TEST(TestHonestSumcheckIdentities);
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
