#include "poly_utils/interpolation.hpp"

#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pEX.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "GaloisRing/utils.hpp"
#include "poly_utils/fft3.hpp"

using NTL::ZZ_pE;
using NTL::ZZ_pEX;
using NTL::coeff;
using NTL::deg;
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

long CheckedLong(std::uint64_t value, const char* label) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

vec_ZZ_pE ToNTLVector(const std::vector<algebra::GRElem>& values) {
  vec_ZZ_pE out;
  out.SetLength(static_cast<long>(values.size()));
  for (long i = 0; i < out.length(); ++i) {
    out[i] = values[static_cast<std::size_t>(i)];
  }
  return out;
}

std::vector<algebra::GRElem> FromNTLPolynomial(const ZZ_pEX& poly) {
  const long poly_degree = deg(poly);
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
  for (std::size_t i = 0; i < points.size(); ++i) {
    for (std::size_t j = i + 1; j < points.size(); ++j) {
      const auto difference =
          ctx.with_ntl_context([&] { return points[i] - points[j]; });
      if (!ctx.is_unit(difference)) {
        throw std::invalid_argument(
            "interpolate_for_gr_wrapper requires an exceptional point set");
      }
    }
  }

  return ctx.with_ntl_context([&] {
    ZZ_pEX poly;
    const vec_ZZ_pE a = ToNTLVector(points);
    const vec_ZZ_pE b = ToNTLVector(values);
    interpolate_for_GR(poly, a, b, ctx.prime(),
                       CheckedLong(ctx.config().k_exp, "k_exp"),
                       CheckedLong(ctx.config().r, "r"));
    return Polynomial(FromNTLPolynomial(poly));
  });
}

}  // namespace swgr::poly_utils
