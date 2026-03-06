#include "poly_utils/fft3.hpp"

#include <stdexcept>

#include "poly_utils/interpolation.hpp"

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

}  // namespace

std::vector<algebra::GRElem> fft3(const Domain& domain,
                                  const Polynomial& poly) {
  if (!IsPowerOfThree(domain.size())) {
    throw std::invalid_argument("poly_utils::fft3 requires a 3-smooth domain");
  }
  return rs_encode(domain, poly);
}

std::vector<algebra::GRElem> inverse_fft3(
    const Domain& domain, const std::vector<algebra::GRElem>& evals) {
  if (!IsPowerOfThree(domain.size())) {
    throw std::invalid_argument(
        "poly_utils::inverse_fft3 requires a 3-smooth domain");
  }
  if (evals.size() != domain.size()) {
    throw std::invalid_argument(
        "poly_utils::inverse_fft3 requires eval count == domain size");
  }

  Polynomial poly = rs_interpolate(domain, evals);
  std::vector<algebra::GRElem> coefficients = poly.coefficients();
  coefficients.resize(static_cast<std::size_t>(domain.size()),
                      domain.context().zero());
  return coefficients;
}

}  // namespace swgr::poly_utils
