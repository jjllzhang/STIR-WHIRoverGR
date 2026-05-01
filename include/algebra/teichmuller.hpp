#ifndef STIR_WHIR_GR_ALGEBRA_TEICHMULLER_HPP_
#define STIR_WHIR_GR_ALGEBRA_TEICHMULLER_HPP_

#include <NTL/ZZ.h>

#include <cstdint>
#include <vector>

#include "algebra/gr_context.hpp"

namespace stir_whir_gr::algebra {

GRElem teichmuller_generator(const GRContext& ctx);

NTL::ZZ teichmuller_group_order(const GRContext& ctx);

NTL::ZZ teichmuller_set_size(const GRContext& ctx);

bool is_teichmuller_element(const GRContext& ctx, const GRElem& element);

// Enumerates the maximal exceptional set as
// `{0, 1, zeta, ..., zeta^(|T|-2)}` where `zeta = teichmuller_generator(ctx)`.
GRElem teichmuller_element_by_index(const GRContext& ctx,
                                    const NTL::ZZ& index);

bool teichmuller_subgroup_size_supported(const GRContext& ctx,
                                         std::uint64_t size);

bool has_exact_multiplicative_order(const GRContext& ctx, const GRElem& element,
                                    std::uint64_t order);

GRElem teichmuller_subgroup_generator(const GRContext& ctx,
                                      std::uint64_t size);

std::vector<GRElem> generate_teichmuller_subgroup(const GRContext& ctx,
                                                  std::uint64_t size);

}  // namespace stir_whir_gr::algebra

#endif  // STIR_WHIR_GR_ALGEBRA_TEICHMULLER_HPP_
