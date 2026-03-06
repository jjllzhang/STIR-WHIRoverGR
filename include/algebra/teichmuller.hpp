#ifndef SWGR_ALGEBRA_TEICHMULLER_HPP_
#define SWGR_ALGEBRA_TEICHMULLER_HPP_

#include <NTL/ZZ.h>

#include <cstdint>
#include <vector>

#include "algebra/gr_context.hpp"

namespace swgr::algebra {

GRElem teichmuller_generator(const GRContext& ctx);

NTL::ZZ teichmuller_group_order(const GRContext& ctx);

bool teichmuller_subgroup_size_supported(const GRContext& ctx,
                                         std::uint64_t size);

bool has_exact_multiplicative_order(const GRContext& ctx, const GRElem& element,
                                    std::uint64_t order);

GRElem teichmuller_subgroup_generator(const GRContext& ctx,
                                      std::uint64_t size);

std::vector<GRElem> generate_teichmuller_subgroup(const GRContext& ctx,
                                                  std::uint64_t size);

}  // namespace swgr::algebra

#endif  // SWGR_ALGEBRA_TEICHMULLER_HPP_
