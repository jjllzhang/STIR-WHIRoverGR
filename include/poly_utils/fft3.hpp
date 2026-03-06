#ifndef SWGR_POLY_UTILS_FFT3_HPP_
#define SWGR_POLY_UTILS_FFT3_HPP_

#include <vector>

#include "domain.hpp"
#include "poly_utils/polynomial.hpp"

namespace swgr::poly_utils {

std::vector<algebra::GRElem> fft3(const Domain& domain,
                                  const Polynomial& poly);

std::vector<algebra::GRElem> inverse_fft3(
    const Domain& domain, const std::vector<algebra::GRElem>& evals);

}  // namespace swgr::poly_utils

#endif  // SWGR_POLY_UTILS_FFT3_HPP_
