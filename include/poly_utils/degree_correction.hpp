#ifndef SWGR_POLY_UTILS_DEGREE_CORRECTION_HPP_
#define SWGR_POLY_UTILS_DEGREE_CORRECTION_HPP_

#include <cstdint>

#include "algebra/gr_context.hpp"
#include "poly_utils/polynomial.hpp"

namespace swgr::poly_utils {

algebra::GRElem degree_correction_eval(
    const algebra::GRContext& ctx,
    const algebra::GRElem& evaluation_point,
    const algebra::GRElem& claimed_eval,
    std::uint64_t target_degree_bound,
    std::uint64_t current_degree_bound,
    const algebra::GRElem& shift_randomness);

Polynomial scaling_polynomial(
    const algebra::GRContext& ctx,
    std::uint64_t target_degree_bound,
    std::uint64_t current_degree_bound,
    const algebra::GRElem& shift_randomness);

Polynomial degree_correction_polynomial(
    const algebra::GRContext& ctx, const Polynomial& poly,
    std::uint64_t target_degree_bound,
    std::uint64_t current_degree_bound,
    const algebra::GRElem& shift_randomness);

}  // namespace swgr::poly_utils

#endif  // SWGR_POLY_UTILS_DEGREE_CORRECTION_HPP_
