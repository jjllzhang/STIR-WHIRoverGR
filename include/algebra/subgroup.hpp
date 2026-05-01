#ifndef STIR_WHIR_GR_ALGEBRA_SUBGROUP_HPP_
#define STIR_WHIR_GR_ALGEBRA_SUBGROUP_HPP_

#include <cstdint>
#include <vector>

#include "algebra/gr_context.hpp"

namespace stir_whir_gr::algebra {

std::vector<GRElem> generate_cyclic_subgroup(const GRContext& ctx,
                                             const GRElem& root,
                                             std::uint64_t size);

std::vector<GRElem> enumerate_cyclic_subgroup(const GRContext& ctx,
                                              const GRElem& root,
                                              std::uint64_t size);

}  // namespace stir_whir_gr::algebra

#endif  // STIR_WHIR_GR_ALGEBRA_SUBGROUP_HPP_
