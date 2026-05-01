#ifndef STIR_WHIR_GR_POLY_UTILS_FFT3_HPP_
#define STIR_WHIR_GR_POLY_UTILS_FFT3_HPP_

#include <vector>

#include "domain.hpp"
#include "poly_utils/polynomial.hpp"

namespace stir_whir_gr::poly_utils {

std::vector<algebra::GRElem> fft3(const Domain& domain,
                                  const Polynomial& poly);

std::vector<algebra::GRElem> inverse_fft3(
    const Domain& domain, const std::vector<algebra::GRElem>& evals);

}  // namespace stir_whir_gr::poly_utils

#endif  // STIR_WHIR_GR_POLY_UTILS_FFT3_HPP_
