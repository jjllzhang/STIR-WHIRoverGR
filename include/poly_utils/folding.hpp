#ifndef SWGR_POLY_UTILS_FOLDING_HPP_
#define SWGR_POLY_UTILS_FOLDING_HPP_

#include <cstdint>
#include <vector>

#include "domain.hpp"
#include "poly_utils/polynomial.hpp"

namespace swgr::poly_utils {

Polynomial poly_fold(const Polynomial& f, std::uint64_t folding_factor_k,
                     const algebra::GRElem& alpha);

algebra::GRElem fold_eval_k(
    const std::vector<algebra::GRElem>& fiber_points,
    const std::vector<algebra::GRElem>& fiber_values,
    const algebra::GRElem& alpha);

std::vector<algebra::GRElem> fold_table_k(
    const Domain& domain, const std::vector<algebra::GRElem>& evals,
    std::uint64_t k_fold, const algebra::GRElem& alpha);

}  // namespace swgr::poly_utils

#endif  // SWGR_POLY_UTILS_FOLDING_HPP_
