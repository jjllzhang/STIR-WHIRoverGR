#include "whir/constraint.hpp"

#include <NTL/ZZ_pE.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "poly_utils/interpolation.hpp"

using NTL::clear;
using NTL::power;
using NTL::set;

namespace stir_whir_gr::whir {
namespace {

constexpr std::size_t kSumcheckDegreePlusOne = 5U;
constexpr std::size_t kTernaryDegreePlusOne = 3U;

using EqPolynomialCoefficients =
    std::array<algebra::GRElem, kTernaryDegreePlusOne>;
using SumcheckCoefficients =
    std::array<algebra::GRElem, kSumcheckDegreePlusOne>;

struct EqCoordinateData {
  EqPolynomialCoefficients eq_coefficients;
  std::array<algebra::GRElem, kTernaryDegreePlusOne> grid_power_sums;
};

std::size_t CheckedSize(std::uint64_t value, const char *label) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds size_t");
  }
  return static_cast<std::size_t>(value);
}

void RequireValidGrid(const algebra::GRContext &ctx, const TernaryGrid &grid) {
  if (!points_have_pairwise_unit_differences(ctx, grid)) {
    throw std::invalid_argument(
        "WHIR ternary grid requires pairwise unit differences");
  }
}

void RequireCompatibleTermSize(const std::vector<EqTerm> &terms,
                               std::span<const algebra::GRElem> point) {
  if (!terms.empty() && terms.front().point.size() != point.size()) {
    throw std::invalid_argument(
        "WhirConstraint requires all terms to have the same arity");
  }
}

void Clear(EqPolynomialCoefficients *coefficients) {
  for (auto &coefficient : *coefficients) {
    clear(coefficient);
  }
}

void Clear(SumcheckCoefficients *coefficients) {
  for (auto &coefficient : *coefficients) {
    clear(coefficient);
  }
}

void AppendGridAssignment(const TernaryGrid &grid, std::uint64_t assignment,
                          std::span<algebra::GRElem> suffix) {
  for (auto &coordinate : suffix) {
    coordinate = grid[static_cast<std::size_t>(assignment % 3U)];
    assignment /= 3U;
  }
}

algebra::GRElem PowSmall(const algebra::GRElem &value, std::uint8_t exponent) {
  if (exponent == 0U) {
    algebra::GRElem out;
    set(out);
    return out;
  }
  if (exponent == 1U) {
    return value;
  }
  if (exponent == 2U) {
    return value * value;
  }
  throw std::invalid_argument("PowSmall supports only exponents 0, 1, 2");
}

EqPolynomialCoefficients LagrangeBasisPolynomial(
    const algebra::GRContext &ctx, const TernaryGrid &grid,
    std::size_t basis_index) {
  if (basis_index >= grid.size()) {
    throw std::out_of_range("lagrange basis index exceeds ternary grid");
  }

  EqPolynomialCoefficients coefficients;
  Clear(&coefficients);
  set(coefficients[0]);
  std::size_t degree = 0;

  algebra::GRElem denominator;
  set(denominator);
  const auto &basis_point = grid[basis_index];
  for (std::size_t i = 0; i < grid.size(); ++i) {
    if (i == basis_index) {
      continue;
    }

    EqPolynomialCoefficients next;
    Clear(&next);
    for (std::size_t d = 0; d <= degree; ++d) {
      next[d] -= coefficients[d] * grid[i];
      next[d + 1U] += coefficients[d];
    }
    coefficients = next;
    ++degree;
    denominator *= basis_point - grid[i];
  }

  const algebra::GRElem denominator_inverse = ctx.inv(denominator);
  for (auto &coefficient : coefficients) {
    coefficient *= denominator_inverse;
  }
  return coefficients;
}

algebra::GRElem EvaluateEqPolynomial(
    const EqPolynomialCoefficients &coefficients, const algebra::GRElem &x) {
  algebra::GRElem out;
  clear(out);
  for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
    out *= x;
    out += *it;
  }
  return out;
}

EqPolynomialCoefficients EqPolynomial(
    const algebra::GRContext &ctx, const TernaryGrid &grid,
    const algebra::GRElem &z) {
  RequireValidGrid(ctx, grid);

  EqPolynomialCoefficients out;
  Clear(&out);
  for (std::size_t i = 0; i < grid.size(); ++i) {
    const algebra::GRElem z_weight =
        lagrange_basis_on_ternary_grid(ctx, grid, i, z);
    const auto basis_coefficients = LagrangeBasisPolynomial(ctx, grid, i);
    for (std::size_t d = 0; d < out.size(); ++d) {
      out[d] += z_weight * basis_coefficients[d];
    }
  }
  return out;
}

EqCoordinateData BuildEqCoordinateData(
    const algebra::GRContext &ctx, const TernaryGrid &grid,
    const algebra::GRElem &z) {
  EqCoordinateData data;
  data.eq_coefficients = EqPolynomial(ctx, grid, z);
  for (auto &sum : data.grid_power_sums) {
    clear(sum);
  }

  for (std::size_t degree = 0; degree < data.grid_power_sums.size(); ++degree) {
    for (const auto &point : grid) {
      const auto power = PowSmall(point, static_cast<std::uint8_t>(degree));
      data.grid_power_sums[degree] +=
          power * EvaluateEqPolynomial(data.eq_coefficients, point);
    }
  }
  return data;
}

std::vector<std::vector<EqCoordinateData>> BuildEqTermData(
    const algebra::GRContext &ctx, const WhirConstraint &constraint,
    std::uint64_t variable_count) {
  std::vector<std::vector<EqCoordinateData>> out;
  out.reserve(constraint.terms().size());
  for (const auto &term : constraint.terms()) {
    if (term.point.size() != CheckedSize(variable_count, "variable_count")) {
      throw std::invalid_argument(
          "honest_sumcheck_polynomial constraint arity mismatch");
    }

    std::vector<EqCoordinateData> term_data;
    term_data.reserve(term.point.size());
    for (const auto &coordinate : term.point) {
      term_data.push_back(BuildEqCoordinateData(ctx, constraint.grid(), coordinate));
    }
    out.push_back(std::move(term_data));
  }
  return out;
}

SumcheckCoefficients ShiftEqPolynomial(
    const EqPolynomialCoefficients &eq_coefficients, std::uint8_t shift) {
  SumcheckCoefficients out;
  Clear(&out);
  if (shift > 2U) {
    throw std::invalid_argument("ShiftEqPolynomial requires shift in [0, 2]");
  }
  for (std::size_t d = 0; d < eq_coefficients.size(); ++d) {
    out[d + shift] += eq_coefficients[d];
  }
  return out;
}

void AddScaled(SumcheckCoefficients *out, const SumcheckCoefficients &term,
               const algebra::GRElem &scale) {
  for (std::size_t d = 0; d < out->size(); ++d) {
    (*out)[d] += scale * term[d];
  }
}

std::vector<algebra::GRElem> TrimTrailingZeros(SumcheckCoefficients coefficients) {
  std::vector<algebra::GRElem> out(coefficients.begin(), coefficients.end());
  while (!out.empty() && out.back() == 0) {
    out.pop_back();
  }
  return out;
}

}  // namespace

TernaryGrid ternary_grid(const algebra::GRContext &ctx,
                         const algebra::GRElem &omega) {
  return ctx.with_ntl_context([&] {
    if (!ctx.is_unit(omega)) {
      throw std::invalid_argument("ternary_grid requires a unit omega");
    }
    if (omega == ctx.one() || power(omega, 3L) != ctx.one()) {
      throw std::invalid_argument("ternary_grid requires omega of order 3");
    }

    TernaryGrid grid{ctx.one(), omega, omega * omega};
    RequireValidGrid(ctx, grid);
    return grid;
  });
}

bool points_have_pairwise_unit_differences(
    const algebra::GRContext &ctx, std::span<const algebra::GRElem> points) {
  return ctx.with_ntl_context([&] {
    for (std::size_t i = 0; i < points.size(); ++i) {
      for (std::size_t j = i + 1U; j < points.size(); ++j) {
        if (!ctx.is_unit(points[i] - points[j])) {
          return false;
        }
      }
    }
    return true;
  });
}

algebra::GRElem lagrange_basis_on_ternary_grid(const algebra::GRContext &ctx,
                                               const TernaryGrid &grid,
                                               std::size_t basis_index,
                                               const algebra::GRElem &x) {
  if (basis_index >= grid.size()) {
    throw std::out_of_range("lagrange basis index exceeds ternary grid");
  }

  return ctx.with_ntl_context([&] {
    RequireValidGrid(ctx, grid);

    algebra::GRElem numerator;
    set(numerator);
    algebra::GRElem denominator;
    set(denominator);
    const auto &basis_point = grid[basis_index];
    for (std::size_t i = 0; i < grid.size(); ++i) {
      if (i == basis_index) {
        continue;
      }
      numerator *= x - grid[i];
      denominator *= basis_point - grid[i];
    }
    return numerator * ctx.inv(denominator);
  });
}

algebra::GRElem eq_B(const algebra::GRContext &ctx, const TernaryGrid &grid,
                     const algebra::GRElem &z, const algebra::GRElem &x) {
  return ctx.with_ntl_context([&] {
    RequireValidGrid(ctx, grid);

    algebra::GRElem out;
    clear(out);
    for (std::size_t i = 0; i < grid.size(); ++i) {
      out += lagrange_basis_on_ternary_grid(ctx, grid, i, z) *
             lagrange_basis_on_ternary_grid(ctx, grid, i, x);
    }
    return out;
  });
}

algebra::GRElem eq_B(const algebra::GRContext &ctx, const TernaryGrid &grid,
                     std::span<const algebra::GRElem> z,
                     std::span<const algebra::GRElem> x) {
  if (z.size() != x.size()) {
    throw std::invalid_argument("eq_B requires equal-length points");
  }

  return ctx.with_ntl_context([&] {
    algebra::GRElem out;
    set(out);
    for (std::size_t i = 0; i < z.size(); ++i) {
      out *= eq_B(ctx, grid, z[i], x[i]);
    }
    return out;
  });
}

std::vector<algebra::GRElem> sumcheck_interpolation_points(
    const algebra::GRContext &ctx) {
  return ctx.with_ntl_context([&] {
    std::vector<algebra::GRElem> points;
    points.reserve(kSumcheckDegreePlusOne);

    algebra::GRElem current;
    set(current);
    const algebra::GRElem generator = ctx.teich_generator();
    for (std::size_t i = 0; i < kSumcheckDegreePlusOne; ++i) {
      points.push_back(current);
      current *= generator;
    }

    if (!points_have_pairwise_unit_differences(ctx, points)) {
      throw std::invalid_argument(
          "sumcheck interpolation points require pairwise unit differences");
    }
    return points;
  });
}

WhirConstraint::WhirConstraint(TernaryGrid grid) : grid_(std::move(grid)) {}

WhirConstraint::WhirConstraint(TernaryGrid grid, std::vector<EqTerm> terms)
    : grid_(std::move(grid)) {
  for (auto &term : terms) {
    add_shift_term(std::move(term.weight), std::move(term.point));
  }
}

std::uint64_t WhirConstraint::variable_count() const {
  if (terms_.empty()) {
    return 0;
  }
  return static_cast<std::uint64_t>(terms_.front().point.size());
}

algebra::GRElem WhirConstraint::evaluate_A(
    const algebra::GRContext &ctx, std::span<const algebra::GRElem> x) const {
  return ctx.with_ntl_context([&] {
    RequireValidGrid(ctx, grid_);

    algebra::GRElem out;
    clear(out);
    for (const auto &term : terms_) {
      if (term.point.size() != x.size()) {
        throw std::invalid_argument(
            "WhirConstraint::evaluate_A point length mismatch");
      }
      out += term.weight * eq_B(ctx, grid_, term.point, x);
    }
    return out;
  });
}

algebra::GRElem WhirConstraint::evaluate_W(
    const algebra::GRContext &ctx, const algebra::GRElem &z,
    std::span<const algebra::GRElem> x) const {
  return ctx.with_ntl_context([&] { return z * evaluate_A(ctx, x); });
}

WhirConstraint WhirConstraint::restrict_prefix(
    const algebra::GRContext &ctx,
    std::span<const algebra::GRElem> alphas) const {
  return ctx.with_ntl_context([&] {
    RequireValidGrid(ctx, grid_);

    std::vector<EqTerm> restricted_terms;
    restricted_terms.reserve(terms_.size());
    for (const auto &term : terms_) {
      if (alphas.size() > term.point.size()) {
        throw std::invalid_argument(
            "WhirConstraint::restrict_prefix fixes too many variables");
      }

      algebra::GRElem restricted_weight = term.weight;
      for (std::size_t i = 0; i < alphas.size(); ++i) {
        restricted_weight *= eq_B(ctx, grid_, term.point[i], alphas[i]);
      }

      std::vector<algebra::GRElem> tail(
          term.point.begin() + static_cast<std::ptrdiff_t>(alphas.size()),
          term.point.end());
      restricted_terms.push_back(
          EqTerm{std::move(restricted_weight), std::move(tail)});
    }
    return WhirConstraint(grid_, std::move(restricted_terms));
  });
}

void WhirConstraint::add_shift_term(algebra::GRElem weight,
                                    std::vector<algebra::GRElem> point) {
  RequireCompatibleTermSize(terms_, point);
  terms_.push_back(EqTerm{std::move(weight), std::move(point)});
}

WhirSumcheckPolynomial honest_sumcheck_polynomial(
    const algebra::GRContext &ctx, std::uint64_t variable_count,
    const WhirConstraint &constraint, std::span<const algebra::GRElem> prefix,
    const PointEvaluator &evaluate_f) {
  if (!evaluate_f) {
    throw std::invalid_argument(
        "honest_sumcheck_polynomial requires an evaluator");
  }
  if (prefix.size() >= CheckedSize(variable_count, "variable_count")) {
    throw std::invalid_argument(
        "honest_sumcheck_polynomial requires one live variable");
  }
  if (!constraint.empty() && constraint.variable_count() != variable_count) {
    throw std::invalid_argument(
        "honest_sumcheck_polynomial constraint arity mismatch");
  }

  return ctx.with_ntl_context([&] {
    const auto points = sumcheck_interpolation_points(ctx);
    std::vector<algebra::GRElem> values;
    values.reserve(points.size());

    const std::uint64_t remaining_count =
        variable_count - static_cast<std::uint64_t>(prefix.size()) - 1U;
    const std::uint64_t assignment_count = pow3_checked(remaining_count);
    const TernaryGrid &grid = constraint.grid();

    std::vector<algebra::GRElem> full_point(
        CheckedSize(variable_count, "variable_count"));
    std::copy(prefix.begin(), prefix.end(), full_point.begin());

    const std::size_t variable_index = prefix.size();
    const std::size_t suffix_begin = variable_index + 1U;
    for (const auto &t : points) {
      full_point[variable_index] = t;

      algebra::GRElem h_t;
      clear(h_t);
      for (std::uint64_t assignment = 0; assignment < assignment_count;
           ++assignment) {
        AppendGridAssignment(
            grid, assignment,
            std::span<algebra::GRElem>(full_point).subspan(suffix_begin));
        h_t += evaluate_f(full_point) * constraint.evaluate_A(ctx, full_point);
      }
      values.push_back(h_t);
    }

    const auto interpolated =
        poly_utils::interpolate_for_gr_wrapper(ctx, points, values);
    return WhirSumcheckPolynomial{interpolated.coefficients()};
  });
}

WhirSumcheckPolynomial honest_sumcheck_polynomial(
    const algebra::GRContext &ctx, const MultiQuadraticPolynomial &polynomial,
    const WhirConstraint &constraint, std::span<const algebra::GRElem> prefix) {
  const std::uint64_t variable_count = polynomial.variable_count();
  if (prefix.size() >= CheckedSize(variable_count, "variable_count")) {
    throw std::invalid_argument(
        "honest_sumcheck_polynomial requires one live variable");
  }
  if (!constraint.empty() && constraint.variable_count() != variable_count) {
    throw std::invalid_argument(
        "honest_sumcheck_polynomial constraint arity mismatch");
  }

  return ctx.with_ntl_context([&] {
    const std::size_t variable_index = prefix.size();
    const auto eq_term_data =
        BuildEqTermData(ctx, constraint, variable_count);

    SumcheckCoefficients out;
    Clear(&out);
    const auto &coefficients = polynomial.coefficients();
    for (std::size_t coefficient_index = 0;
         coefficient_index < coefficients.size(); ++coefficient_index) {
      const auto &coefficient = coefficients[coefficient_index];
      if (coefficient == 0) {
        continue;
      }

      std::uint64_t digits = static_cast<std::uint64_t>(coefficient_index);
      std::array<std::uint8_t, 64> digit_cache{};
      if (variable_count > digit_cache.size()) {
        throw std::invalid_argument(
            "honest_sumcheck_polynomial variable_count exceeds digit cache");
      }
      for (std::uint64_t variable = 0; variable < variable_count; ++variable) {
        digit_cache[CheckedSize(variable, "variable")] =
            static_cast<std::uint8_t>(digits % 3U);
        digits /= 3U;
      }

      for (std::size_t term_index = 0; term_index < constraint.terms().size();
           ++term_index) {
        const auto &term = constraint.terms()[term_index];
        const auto &term_data = eq_term_data[term_index];
        algebra::GRElem scalar = coefficient * term.weight;
        for (std::size_t variable = 0; variable < variable_index; ++variable) {
          const std::uint8_t digit = digit_cache[variable];
          scalar *= PowSmall(prefix[variable], digit) *
                    EvaluateEqPolynomial(term_data[variable].eq_coefficients,
                                         prefix[variable]);
        }
        for (std::size_t variable = variable_index + 1U;
             variable < CheckedSize(variable_count, "variable_count");
             ++variable) {
          const std::uint8_t digit = digit_cache[variable];
          scalar *= term_data[variable].grid_power_sums[digit];
        }
        const std::uint8_t live_digit = digit_cache[variable_index];
        const auto live_polynomial = ShiftEqPolynomial(
            term_data[variable_index].eq_coefficients, live_digit);
        AddScaled(&out, live_polynomial, scalar);
      }
    }

    return WhirSumcheckPolynomial{TrimTrailingZeros(out)};
  });
}

std::size_t sumcheck_declared_degree(const WhirSumcheckPolynomial &polynomial) {
  if (polynomial.coefficients.empty()) {
    return 0;
  }
  return polynomial.coefficients.size() - 1U;
}

algebra::GRElem evaluate_sumcheck_polynomial(
    const algebra::GRContext &ctx, const WhirSumcheckPolynomial &polynomial,
    const algebra::GRElem &x) {
  return ctx.with_ntl_context([&] {
    algebra::GRElem out;
    clear(out);
    for (auto it = polynomial.coefficients.rbegin();
         it != polynomial.coefficients.rend(); ++it) {
      out *= x;
      out += *it;
    }
    return out;
  });
}

bool check_sumcheck_degree(const WhirSumcheckPolynomial &polynomial,
                           std::uint64_t degree_bound) {
  return sumcheck_declared_degree(polynomial) <=
         CheckedSize(degree_bound, "degree_bound");
}

algebra::GRElem sumcheck_grid_sum(const algebra::GRContext &ctx,
                                  const TernaryGrid &grid,
                                  const WhirSumcheckPolynomial &polynomial) {
  return ctx.with_ntl_context([&] {
    RequireValidGrid(ctx, grid);

    algebra::GRElem out;
    clear(out);
    for (const auto &point : grid) {
      out += evaluate_sumcheck_polynomial(ctx, polynomial, point);
    }
    return out;
  });
}

bool check_sumcheck_identity(const algebra::GRContext &ctx,
                             const TernaryGrid &grid,
                             const WhirSumcheckPolynomial &polynomial,
                             const algebra::GRElem &current_sigma,
                             std::uint64_t degree_bound) {
  if (!check_sumcheck_degree(polynomial, degree_bound)) {
    return false;
  }
  return sumcheck_grid_sum(ctx, grid, polynomial) == current_sigma;
}

algebra::GRElem sumcheck_next_sigma(const algebra::GRContext &ctx,
                                    const WhirSumcheckPolynomial &polynomial,
                                    const algebra::GRElem &alpha) {
  return evaluate_sumcheck_polynomial(ctx, polynomial, alpha);
}

}  // namespace stir_whir_gr::whir
