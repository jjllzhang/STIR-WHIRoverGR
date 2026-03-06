#include "poly_utils/quotient.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "poly_utils/interpolation.hpp"

namespace swgr::poly_utils {
namespace {

std::vector<algebra::GRElem> Trim(std::vector<algebra::GRElem> coefficients) {
  while (!coefficients.empty() && coefficients.back() == 0) {
    coefficients.pop_back();
  }
  return coefficients;
}

Polynomial Subtract(const algebra::GRContext& ctx, const Polynomial& lhs,
                    const Polynomial& rhs) {
  return ctx.with_ntl_context([&] {
    const auto& lhs_coefficients = lhs.coefficients();
    const auto& rhs_coefficients = rhs.coefficients();
    const std::size_t out_size =
        std::max(lhs_coefficients.size(), rhs_coefficients.size());
    std::vector<algebra::GRElem> coefficients(out_size, ctx.zero());
    for (std::size_t i = 0; i < lhs_coefficients.size(); ++i) {
      coefficients[i] += lhs_coefficients[i];
    }
    for (std::size_t i = 0; i < rhs_coefficients.size(); ++i) {
      coefficients[i] -= rhs_coefficients[i];
    }
    return Polynomial(Trim(std::move(coefficients)));
  });
}

Polynomial DivideExactlyByMonic(const algebra::GRContext& ctx,
                                const Polynomial& numerator,
                                const Polynomial& denominator) {
  return ctx.with_ntl_context([&] {
    auto numerator_coefficients = Trim(numerator.coefficients());
    const auto denominator_coefficients = Trim(denominator.coefficients());

    if (denominator_coefficients.empty()) {
      throw std::invalid_argument(
          "quotient_polynomial requires non-zero denominator");
    }
    if (numerator_coefficients.empty()) {
      return Polynomial{};
    }

    const auto& denominator_lead = denominator_coefficients.back();
    if (!ctx.is_unit(denominator_lead)) {
      throw std::invalid_argument(
          "quotient_polynomial requires denominator leading coefficient unit");
    }
    const auto denominator_lead_inverse = ctx.inv(denominator_lead);

    if (numerator_coefficients.size() < denominator_coefficients.size()) {
      throw std::invalid_argument(
          "quotient_polynomial received numerator degree smaller than denominator");
    }

    std::vector<algebra::GRElem> quotient(
        numerator_coefficients.size() - denominator_coefficients.size() + 1,
        ctx.zero());

    while (!numerator_coefficients.empty() &&
           numerator_coefficients.size() >= denominator_coefficients.size()) {
      const std::size_t offset =
          numerator_coefficients.size() - denominator_coefficients.size();
      const auto factor =
          numerator_coefficients.back() * denominator_lead_inverse;
      quotient[offset] += factor;

      for (std::size_t i = 0; i < denominator_coefficients.size(); ++i) {
        numerator_coefficients[offset + i] -= factor * denominator_coefficients[i];
      }
      numerator_coefficients = Trim(std::move(numerator_coefficients));
    }

    if (!numerator_coefficients.empty()) {
      throw std::invalid_argument(
          "quotient_polynomial requires exact divisibility");
    }

    return Polynomial(Trim(std::move(quotient)));
  });
}

algebra::GRElem DenominatorInverse(
    const algebra::GRContext& ctx, const algebra::GRElem& evaluation_point,
    const std::vector<algebra::GRElem>& quotient_set) {
  return ctx.with_ntl_context([&] {
    auto denominator = ctx.one();
    for (const auto& point : quotient_set) {
      if (evaluation_point == point) {
        throw std::invalid_argument(
            "quotient evaluation point must lie outside quotient set");
      }
      const auto difference = evaluation_point - point;
      if (!ctx.is_unit(difference)) {
        throw std::invalid_argument(
            "quotient denominator requires exceptional quotient set");
      }
      denominator *= difference;
    }
    if (!ctx.is_unit(denominator)) {
      throw std::invalid_argument(
          "quotient denominator product must be invertible");
    }
    return ctx.inv(denominator);
  });
}

}  // namespace

Polynomial answer_polynomial(const algebra::GRContext& ctx,
                             const std::vector<algebra::GRElem>& points,
                             const std::vector<algebra::GRElem>& values) {
  if (points.empty()) {
    return Polynomial{};
  }
  return interpolate_for_gr_wrapper(ctx, points, values);
}

Polynomial vanishing_polynomial(const algebra::GRContext& ctx,
                                const std::vector<algebra::GRElem>& points) {
  return ctx.with_ntl_context([&] {
    std::vector<algebra::GRElem> coefficients{ctx.one()};
    for (const auto& point : points) {
      std::vector<algebra::GRElem> next(coefficients.size() + 1, ctx.zero());
      for (std::size_t i = 0; i < coefficients.size(); ++i) {
        next[i] -= coefficients[i] * point;
        next[i + 1] += coefficients[i];
      }
      coefficients = std::move(next);
    }
    return Polynomial(Trim(std::move(coefficients)));
  });
}

Polynomial quotient_polynomial(const algebra::GRContext& ctx,
                               const Polynomial& numerator,
                               const Polynomial& denominator) {
  return DivideExactlyByMonic(ctx, numerator, denominator);
}

Polynomial quotient_polynomial_from_answers(
    const algebra::GRContext& ctx, const Polynomial& poly,
    const std::vector<algebra::GRElem>& points,
    const std::vector<algebra::GRElem>& values) {
  if (points.size() != values.size()) {
    throw std::invalid_argument(
        "quotient_polynomial_from_answers requires equal-sized inputs");
  }
  if (points.empty()) {
    return poly;
  }

  const auto ans = answer_polynomial(ctx, points, values);
  const auto vanishing = vanishing_polynomial(ctx, points);
  const auto numerator = Subtract(ctx, poly, ans);
  if (numerator.empty()) {
    return Polynomial{};
  }
  return quotient_polynomial(ctx, numerator, vanishing);
}

algebra::GRElem quotient_eval(
    const algebra::GRContext& ctx, const algebra::GRElem& claimed_eval,
    const algebra::GRElem& evaluation_point,
    const std::vector<std::pair<algebra::GRElem, algebra::GRElem>>& answers) {
  std::vector<algebra::GRElem> points;
  std::vector<algebra::GRElem> values;
  points.reserve(answers.size());
  values.reserve(answers.size());
  for (const auto& [point, value] : answers) {
    points.push_back(point);
    values.push_back(value);
  }
  const auto denominator_inverse =
      DenominatorInverse(ctx, evaluation_point, points);
  const auto ans = answer_polynomial(ctx, points, values);
  const auto answer_value = ans.evaluate(ctx, evaluation_point);
  return quotient_eval_with_hint(ctx, claimed_eval, evaluation_point, points,
                                 denominator_inverse, answer_value);
}

algebra::GRElem quotient_eval_with_hint(
    const algebra::GRContext& ctx, const algebra::GRElem& claimed_eval,
    const algebra::GRElem& evaluation_point,
    const std::vector<algebra::GRElem>& quotient_set,
    const algebra::GRElem& denominator_inverse_hint,
    const algebra::GRElem& answer_eval) {
  (void)evaluation_point;
  if (denominator_inverse_hint == 0) {
    throw std::invalid_argument(
        "quotient_eval_with_hint requires a non-zero inverse hint");
  }

  return ctx.with_ntl_context([&] {
    for (const auto& point : quotient_set) {
      if (evaluation_point == point) {
        throw std::invalid_argument(
            "quotient evaluation point must lie outside quotient set");
      }
    }
    return (claimed_eval - answer_eval) * denominator_inverse_hint;
  });
}

}  // namespace swgr::poly_utils
