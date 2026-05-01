#ifndef STIR_WHIR_GR_WHIR_FOLDING_HPP_
#define STIR_WHIR_GR_WHIR_FOLDING_HPP_

#include <cstdint>
#include <span>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"

namespace stir_whir_gr::whir {

std::vector<algebra::GRElem> repeated_ternary_fold_table(
    const Domain& domain, const std::vector<algebra::GRElem>& evals,
    std::span<const algebra::GRElem> alphas);

std::vector<std::uint64_t> virtual_fold_query_indices(
    std::uint64_t domain_size, std::uint64_t b, std::uint64_t child_index);

// The points and values must use the order returned by
// virtual_fold_query_indices for the corresponding query fiber. This function
// expects an active NTL context matching the supplied ring elements.
algebra::GRElem evaluate_repeated_ternary_fold_from_values(
    const std::vector<algebra::GRElem>& points,
    const std::vector<algebra::GRElem>& values,
    std::span<const algebra::GRElem> alphas);

algebra::GRElem evaluate_virtual_fold_query_from_leaf_payloads(
    const Domain& domain, std::uint64_t b, std::uint64_t child_index,
    std::span<const std::vector<std::uint8_t>> leaf_payloads,
    std::span<const algebra::GRElem> alphas);

}  // namespace stir_whir_gr::whir

#endif  // STIR_WHIR_GR_WHIR_FOLDING_HPP_
