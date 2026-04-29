#include "whir/multiquadratic.hpp"

#include <NTL/ZZ_pE.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

using NTL::clear;
using NTL::set;

namespace swgr::whir {
namespace {

std::size_t CheckedSize(std::uint64_t value, const char* label) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds size_t");
  }
  return static_cast<std::size_t>(value);
}

std::vector<algebra::GRElem> TrimTrailingZeros(
    std::vector<algebra::GRElem> coefficients) {
  while (!coefficients.empty() && coefficients.back() == 0) {
    coefficients.pop_back();
  }
  return coefficients;
}

algebra::GRElem PrefixWeight(std::uint64_t prefix_index,
                             std::span<const algebra::GRElem> alphas) {
  algebra::GRElem weight;
  set(weight);

  for (const auto& alpha : alphas) {
    const std::uint8_t digit = static_cast<std::uint8_t>(prefix_index % 3U);
    if (digit == 1U) {
      weight *= alpha;
    } else if (digit == 2U) {
      weight *= alpha * alpha;
    }
    prefix_index /= 3U;
  }
  return weight;
}

}  // namespace

std::uint64_t pow3_checked(std::uint64_t exponent) {
  std::uint64_t out = 1;
  for (std::uint64_t i = 0; i < exponent; ++i) {
    if (out > std::numeric_limits<std::uint64_t>::max() / 3U) {
      throw std::overflow_error("pow3_checked overflow");
    }
    out *= 3U;
  }
  return out;
}

std::uint64_t encode_base3_index(std::span<const std::uint8_t> digits) {
  std::uint64_t index = 0;
  std::uint64_t place = 1;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    const auto digit = digits[i];
    if (digit > 2U) {
      throw std::invalid_argument(
          "encode_base3_index requires digits in [0, 2]");
    }
    if (digit != 0U &&
        place > (std::numeric_limits<std::uint64_t>::max() - index) / digit) {
      throw std::overflow_error("encode_base3_index overflow");
    }
    index += place * digit;
    if (i + 1U < digits.size()) {
      if (place > std::numeric_limits<std::uint64_t>::max() / 3U) {
        throw std::overflow_error("encode_base3_index overflow");
      }
      place *= 3U;
    }
  }
  return index;
}

std::vector<std::uint8_t> decode_base3_index(std::uint64_t index,
                                             std::uint64_t digit_count) {
  const std::uint64_t bound = pow3_checked(digit_count);
  if (index >= bound) {
    throw std::out_of_range("decode_base3_index index exceeds digit count");
  }

  std::vector<std::uint8_t> digits;
  digits.reserve(CheckedSize(digit_count, "digit_count"));
  for (std::uint64_t i = 0; i < digit_count; ++i) {
    digits.push_back(static_cast<std::uint8_t>(index % 3U));
    index /= 3U;
  }
  return digits;
}

std::vector<algebra::GRElem> pow_m(const algebra::GRContext& ctx,
                                   const algebra::GRElem& x,
                                   std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<algebra::GRElem> powers;
    powers.reserve(CheckedSize(variable_count, "variable_count"));

    auto current = x;
    for (std::uint64_t i = 0; i < variable_count; ++i) {
      powers.push_back(current);
      current = current * current * current;
    }
    return powers;
  });
}

MultiQuadraticPolynomial::MultiQuadraticPolynomial(
    std::uint64_t variable_count, std::vector<algebra::GRElem> coefficients)
    : variable_count_(variable_count) {
  const std::uint64_t max_coefficients = pow3_checked(variable_count_);
  if (coefficients.size() >
      CheckedSize(max_coefficients, "coefficient bound")) {
    throw std::invalid_argument(
        "MultiQuadraticPolynomial coefficient length exceeds 3^m");
  }
  coefficients_ = TrimTrailingZeros(std::move(coefficients));
}

algebra::GRElem MultiQuadraticPolynomial::evaluate(
    const algebra::GRContext& ctx,
    std::span<const algebra::GRElem> point) const {
  if (point.size() != CheckedSize(variable_count_, "variable_count")) {
    throw std::invalid_argument(
        "MultiQuadraticPolynomial::evaluate point length mismatch");
  }

  return ctx.with_ntl_context([&] {
    algebra::GRElem acc;
    clear(acc);

    for (std::size_t index = 0; index < coefficients_.size(); ++index) {
      const auto& coefficient = coefficients_[index];
      if (coefficient == 0) {
        continue;
      }

      std::uint64_t digits = static_cast<std::uint64_t>(index);
      algebra::GRElem monomial;
      set(monomial);
      for (std::uint64_t variable = 0; variable < variable_count_; ++variable) {
        const std::uint8_t digit = static_cast<std::uint8_t>(digits % 3U);
        const auto& coordinate = point[CheckedSize(variable, "variable")];
        if (digit == 1U) {
          monomial *= coordinate;
        } else if (digit == 2U) {
          monomial *= coordinate * coordinate;
        }
        digits /= 3U;
      }
      acc += coefficient * monomial;
    }
    return acc;
  });
}

algebra::GRElem MultiQuadraticPolynomial::evaluate_pow(
    const algebra::GRContext& ctx, const algebra::GRElem& x) const {
  return ctx.with_ntl_context([&] {
    algebra::GRElem acc;
    clear(acc);
    for (auto it = coefficients_.rbegin(); it != coefficients_.rend(); ++it) {
      acc *= x;
      acc += *it;
    }
    return acc;
  });
}

MultiQuadraticPolynomial MultiQuadraticPolynomial::restrict_prefix(
    const algebra::GRContext& ctx,
    std::span<const algebra::GRElem> alphas) const {
  if (alphas.size() > CheckedSize(variable_count_, "variable_count")) {
    throw std::invalid_argument(
        "MultiQuadraticPolynomial::restrict_prefix fixes too many variables");
  }

  const std::uint64_t fixed_count = static_cast<std::uint64_t>(alphas.size());
  const std::uint64_t remaining_count = variable_count_ - fixed_count;
  const std::uint64_t prefix_size = pow3_checked(fixed_count);
  const std::uint64_t tail_bound = pow3_checked(remaining_count);

  return ctx.with_ntl_context([&] {
    if (coefficients_.empty()) {
      return MultiQuadraticPolynomial(remaining_count, {});
    }

    const std::uint64_t required_tail_terms =
        (static_cast<std::uint64_t>(coefficients_.size()) + prefix_size - 1U) /
        prefix_size;
    const std::uint64_t output_terms =
        std::min(required_tail_terms, tail_bound);

    std::vector<algebra::GRElem> restricted(
        CheckedSize(output_terms, "restricted coefficient count"));
    for (auto& coefficient : restricted) {
      clear(coefficient);
    }

    for (std::size_t index = 0; index < coefficients_.size(); ++index) {
      const std::uint64_t flat_index = static_cast<std::uint64_t>(index);
      const std::uint64_t prefix_index = flat_index % prefix_size;
      const std::uint64_t tail_index = flat_index / prefix_size;
      restricted[CheckedSize(tail_index, "tail index")] +=
          coefficients_[index] * PrefixWeight(prefix_index, alphas);
    }

    return MultiQuadraticPolynomial(remaining_count, std::move(restricted));
  });
}

poly_utils::Polynomial MultiQuadraticPolynomial::to_univariate_pow_polynomial(
    const algebra::GRContext& ctx) const {
  return ctx.with_ntl_context(
      [&] { return poly_utils::Polynomial(coefficients_); });
}

}  // namespace swgr::whir
