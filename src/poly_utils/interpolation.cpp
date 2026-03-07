#include "poly_utils/interpolation.hpp"

#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pEX.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "poly_utils/fft3.hpp"

using NTL::ZZ_pE;
using NTL::ZZ_pEX;
using NTL::clear;
using NTL::coeff;
using NTL::set;
using NTL::vec_ZZ_pE;

namespace swgr::poly_utils {
namespace {

bool IsPowerOfThree(std::uint64_t value) {
  if (value == 0) {
    return false;
  }
  while (value % 3 == 0) {
    value /= 3;
  }
  return value == 1;
}

algebra::GRElem EncodeUnsigned(std::uint64_t value) {
  algebra::GRElem result;
  clear(result);
  algebra::GRElem addend;
  set(addend);

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
}

vec_ZZ_pE ToNTLVector(const std::vector<algebra::GRElem>& values) {
  vec_ZZ_pE out;
  out.SetLength(static_cast<long>(values.size()));
  for (long i = 0; i < out.length(); ++i) {
    out[i] = values[static_cast<std::size_t>(i)];
  }
  return out;
}

std::vector<algebra::GRElem> Trim(std::vector<algebra::GRElem> coefficients) {
  while (!coefficients.empty() && coefficients.back() == 0) {
    coefficients.pop_back();
  }
  return coefficients;
}

std::vector<algebra::GRElem> FromNTLPolynomial(const ZZ_pEX& poly) {
  const long poly_degree = NTL::deg(poly);
  if (poly_degree < 0) {
    return {};
  }

  std::vector<algebra::GRElem> out;
  out.reserve(static_cast<std::size_t>(poly_degree + 1));
  for (long i = 0; i <= poly_degree; ++i) {
    out.push_back(coeff(poly, i));
  }
  return out;
}

std::vector<algebra::GRElem> MultiplyByMonicLinear(
    const std::vector<algebra::GRElem>& coefficients,
    const algebra::GRElem& root) {
  std::vector<algebra::GRElem> next(coefficients.size() + 1U);
  for (auto& coefficient : next) {
    clear(coefficient);
  }

  for (std::size_t i = 0; i < coefficients.size(); ++i) {
    next[i] -= coefficients[i] * root;
    next[i + 1U] += coefficients[i];
  }
  return next;
}

std::vector<algebra::GRElem> DerivativeCoefficients(
    const std::vector<algebra::GRElem>& coefficients) {
  if (coefficients.size() <= 1U) {
    return {};
  }

  std::vector<algebra::GRElem> derivative(coefficients.size() - 1U);
  for (std::size_t i = 1; i < coefficients.size(); ++i) {
    derivative[i - 1U] = coefficients[i] * EncodeUnsigned(i);
  }
  return Trim(std::move(derivative));
}

algebra::GRElem EvaluatePolynomial(
    const std::vector<algebra::GRElem>& coefficients,
    const algebra::GRElem& point) {
  algebra::GRElem acc;
  clear(acc);

  for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
    acc *= point;
    acc += *it;
  }
  return acc;
}

std::vector<algebra::GRElem> DivideByMonicLinear(
    const std::vector<algebra::GRElem>& coefficients,
    const algebra::GRElem& root) {
  if (coefficients.size() <= 1U) {
    return {};
  }

  const std::size_t quotient_size = coefficients.size() - 1U;
  std::vector<algebra::GRElem> quotient(quotient_size);
  quotient[quotient_size - 1U] = coefficients.back();
  for (std::size_t i = quotient_size - 1U; i > 0; --i) {
    quotient[i - 1U] = coefficients[i] + root * quotient[i];
  }
  return Trim(std::move(quotient));
}

}  // namespace

std::vector<algebra::GRElem> rs_encode(const Domain& domain,
                                       const Polynomial& poly) {
  if (IsPowerOfThree(domain.size())) {
    return fft3(domain, poly);
  }

  const auto points = domain.elements();
  std::vector<algebra::GRElem> evals;
  evals.reserve(points.size());
  for (const auto& x : points) {
    evals.push_back(poly.evaluate(domain.context(), x));
  }
  return evals;
}

Polynomial rs_interpolate(const Domain& domain,
                          const std::vector<algebra::GRElem>& evals) {
  if (IsPowerOfThree(domain.size())) {
    return Polynomial(inverse_fft3(domain, evals));
  }
  return interpolate_for_gr_wrapper(domain.context(), domain.elements(), evals);
}

Polynomial interpolate_for_gr_wrapper(
    const algebra::GRContext& ctx, const std::vector<algebra::GRElem>& points,
    const std::vector<algebra::GRElem>& values) {
  if (points.empty()) {
    throw std::invalid_argument("interpolate_for_gr_wrapper requires points");
  }
  if (points.size() != values.size()) {
    throw std::invalid_argument(
        "interpolate_for_gr_wrapper requires equal-sized inputs");
  }

  return ctx.with_ntl_context([&] {
    if (ctx.config().k_exp == 1U) {
      for (std::size_t i = 0; i < points.size(); ++i) {
        for (std::size_t j = i + 1U; j < points.size(); ++j) {
          if (points[i] == points[j]) {
            throw std::invalid_argument(
                "interpolate_for_gr_wrapper requires an exceptional point set");
          }
        }
      }

      ZZ_pEX poly;
      const vec_ZZ_pE a = ToNTLVector(points);
      const vec_ZZ_pE b = ToNTLVector(values);
      NTL::interpolate(poly, a, b);
      return Polynomial(FromNTLPolynomial(poly));
    }

    std::vector<algebra::GRElem> vanishing;
    vanishing.reserve(points.size() + 1U);
    vanishing.emplace_back();
    set(vanishing.front());
    for (const auto& point : points) {
      vanishing = MultiplyByMonicLinear(vanishing, point);
    }

    const auto derivative = DerivativeCoefficients(vanishing);
    std::vector<algebra::GRElem> derivative_values;
    derivative_values.reserve(points.size());
    for (const auto& point : points) {
      derivative_values.push_back(EvaluatePolynomial(derivative, point));
    }

    std::vector<algebra::GRElem> barycentric_weights;
    try {
      barycentric_weights = ctx.batch_inv(derivative_values);
    } catch (const std::invalid_argument&) {
      throw std::invalid_argument(
          "interpolate_for_gr_wrapper requires an exceptional point set");
    }

    std::vector<algebra::GRElem> coefficients(points.size());
    for (auto& coefficient : coefficients) {
      clear(coefficient);
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
      const auto basis = DivideByMonicLinear(vanishing, points[i]);
      const auto scale = values[i] * barycentric_weights[i];
      for (std::size_t j = 0; j < basis.size(); ++j) {
        coefficients[j] += basis[j] * scale;
      }
    }

    return Polynomial(Trim(std::move(coefficients)));
  });
}

}  // namespace swgr::poly_utils
