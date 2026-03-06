#include "poly_utils/degree_correction.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace swgr::poly_utils {
namespace {

std::vector<algebra::GRElem> Trim(std::vector<algebra::GRElem> coefficients) {
  while (!coefficients.empty() && coefficients.back() == 0) {
    coefficients.pop_back();
  }
  return coefficients;
}

algebra::GRElem Pow(const algebra::GRContext& ctx, algebra::GRElem base,
                    std::uint64_t exponent) {
  return ctx.with_ntl_context([&] {
    auto result = ctx.one();
    while (exponent > 0) {
      if ((exponent & 1U) != 0U) {
        result *= base;
      }
      exponent >>= 1U;
      if (exponent != 0) {
        base *= base;
      }
    }
    return result;
  });
}

algebra::GRElem EncodeUnsigned(const algebra::GRContext& ctx,
                               std::uint64_t value) {
  return ctx.with_ntl_context([&] {
    auto result = ctx.zero();
    auto addend = ctx.one();
    while (value > 0) {
      if ((value & 1U) != 0U) {
        result += addend;
      }
      value >>= 1U;
      if (value != 0) {
        addend += addend;
      }
    }
    return result;
  });
}

Polynomial Multiply(const algebra::GRContext& ctx, const Polynomial& lhs,
                    const Polynomial& rhs) {
  return ctx.with_ntl_context([&] {
    if (lhs.empty() || rhs.empty()) {
      return Polynomial{};
    }

    std::vector<algebra::GRElem> coefficients(
        lhs.coefficients().size() + rhs.coefficients().size() - 1, ctx.zero());
    for (std::size_t i = 0; i < lhs.coefficients().size(); ++i) {
      for (std::size_t j = 0; j < rhs.coefficients().size(); ++j) {
        coefficients[i + j] += lhs.coefficients()[i] * rhs.coefficients()[j];
      }
    }
    return Polynomial(Trim(std::move(coefficients)));
  });
}

}  // namespace

algebra::GRElem degree_correction_eval(
    const algebra::GRContext& ctx,
    const algebra::GRElem& evaluation_point,
    const algebra::GRElem& claimed_eval,
    std::uint64_t target_degree_bound,
    std::uint64_t current_degree_bound,
    const algebra::GRElem& shift_randomness) {
  if (current_degree_bound > target_degree_bound) {
    throw std::invalid_argument(
        "degree_correction_eval requires current_degree_bound <= target_degree_bound");
  }

  const std::uint64_t exponent_gap =
      target_degree_bound - current_degree_bound;
  return ctx.with_ntl_context([&] {
    const auto common_factor = evaluation_point * shift_randomness;
    const auto one = ctx.one();
    const auto denominator = one - common_factor;
    if (denominator == 0) {
      return claimed_eval * EncodeUnsigned(ctx, exponent_gap + 1);
    }
    if (!ctx.is_unit(denominator)) {
      throw std::invalid_argument(
          "degree_correction_eval requires invertible denominator");
    }
    const auto numerator = one - Pow(ctx, common_factor, exponent_gap + 1);
    return claimed_eval * numerator * ctx.inv(denominator);
  });
}

Polynomial scaling_polynomial(
    const algebra::GRContext& ctx,
    std::uint64_t target_degree_bound,
    std::uint64_t current_degree_bound,
    const algebra::GRElem& shift_randomness) {
  if (current_degree_bound > target_degree_bound) {
    throw std::invalid_argument(
        "scaling_polynomial requires current_degree_bound <= target_degree_bound");
  }

  const std::uint64_t exponent_gap =
      target_degree_bound - current_degree_bound;
  return ctx.with_ntl_context([&] {
    std::vector<algebra::GRElem> coefficients;
    coefficients.reserve(static_cast<std::size_t>(exponent_gap + 1));
    auto power = ctx.one();
    for (std::uint64_t i = 0; i <= exponent_gap; ++i) {
      coefficients.push_back(power);
      power *= shift_randomness;
    }
    return Polynomial(Trim(std::move(coefficients)));
  });
}

Polynomial degree_correction_polynomial(
    const algebra::GRContext& ctx, const Polynomial& poly,
    std::uint64_t target_degree_bound,
    std::uint64_t current_degree_bound,
    const algebra::GRElem& shift_randomness) {
  if (poly.empty()) {
    return Polynomial{};
  }
  const auto scaling = scaling_polynomial(
      ctx, target_degree_bound, current_degree_bound, shift_randomness);
  return Multiply(ctx, poly, scaling);
}

}  // namespace swgr::poly_utils
