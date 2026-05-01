#ifndef STIR_WHIR_GR_POLY_UTILS_INTERPOLATION_HPP_
#define STIR_WHIR_GR_POLY_UTILS_INTERPOLATION_HPP_

#include <vector>

#include "domain.hpp"
#include "poly_utils/polynomial.hpp"

namespace stir_whir_gr::poly_utils {

std::vector<algebra::GRElem> rs_encode(const Domain& domain,
                                       const Polynomial& poly);

Polynomial rs_interpolate(const Domain& domain,
                          const std::vector<algebra::GRElem>& evals);

Polynomial interpolate_for_gr_wrapper(
    const algebra::GRContext& ctx, const std::vector<algebra::GRElem>& points,
    const std::vector<algebra::GRElem>& values);

}  // namespace stir_whir_gr::poly_utils

#endif  // STIR_WHIR_GR_POLY_UTILS_INTERPOLATION_HPP_
