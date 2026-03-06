#ifndef SWGR_POLY_UTILS_QUOTIENT_HPP_
#define SWGR_POLY_UTILS_QUOTIENT_HPP_

#include "poly_utils/polynomial.hpp"

namespace swgr::poly_utils {

Polynomial quotient_polynomial(const Polynomial& numerator,
                               const Polynomial& denominator);

}  // namespace swgr::poly_utils

#endif  // SWGR_POLY_UTILS_QUOTIENT_HPP_
