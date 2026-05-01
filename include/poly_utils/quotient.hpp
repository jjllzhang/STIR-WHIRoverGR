#ifndef STIR_WHIR_GR_POLY_UTILS_QUOTIENT_HPP_
#define STIR_WHIR_GR_POLY_UTILS_QUOTIENT_HPP_

#include <utility>
#include <vector>

#include "algebra/gr_context.hpp"
#include "poly_utils/polynomial.hpp"

namespace stir_whir_gr::poly_utils {

Polynomial answer_polynomial(
    const algebra::GRContext& ctx,
    const std::vector<algebra::GRElem>& points,
    const std::vector<algebra::GRElem>& values);

Polynomial vanishing_polynomial(
    const algebra::GRContext& ctx,
    const std::vector<algebra::GRElem>& points);

Polynomial quotient_polynomial(const algebra::GRContext& ctx,
                               const Polynomial& numerator,
                               const Polynomial& denominator);

Polynomial quotient_polynomial_from_answers(
    const algebra::GRContext& ctx, const Polynomial& poly,
    const std::vector<algebra::GRElem>& points,
    const std::vector<algebra::GRElem>& values);

algebra::GRElem quotient_eval(
    const algebra::GRContext& ctx,
    const algebra::GRElem& claimed_eval,
    const algebra::GRElem& evaluation_point,
    const std::vector<std::pair<algebra::GRElem, algebra::GRElem>>& answers);

algebra::GRElem quotient_eval_with_hint(
    const algebra::GRContext& ctx,
    const algebra::GRElem& claimed_eval,
    const algebra::GRElem& evaluation_point,
    const std::vector<algebra::GRElem>& quotient_set,
    const algebra::GRElem& denominator_inverse_hint,
    const algebra::GRElem& answer_eval);

}  // namespace stir_whir_gr::poly_utils

#endif  // STIR_WHIR_GR_POLY_UTILS_QUOTIENT_HPP_
