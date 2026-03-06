#ifndef SWGR_ALGEBRA_SUBGROUP_HPP_
#define SWGR_ALGEBRA_SUBGROUP_HPP_

#include <cstdint>
#include <vector>

#include "algebra/gr_context.hpp"

namespace swgr::algebra {

std::vector<GRElem> generate_cyclic_subgroup(const GRContext& ctx,
                                             const GRElem& root,
                                             std::uint64_t size);

std::vector<GRElem> enumerate_cyclic_subgroup(const GRContext& ctx,
                                              const GRElem& root,
                                              std::uint64_t size);

}  // namespace swgr::algebra

#endif  // SWGR_ALGEBRA_SUBGROUP_HPP_
