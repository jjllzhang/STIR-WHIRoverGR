#ifndef SWGR_ALGEBRA_GR_SERIALIZATION_HPP_
#define SWGR_ALGEBRA_GR_SERIALIZATION_HPP_

#include <cstdint>
#include <span>
#include <vector>

#include "algebra/gr_context.hpp"

namespace swgr::algebra {

std::vector<std::uint8_t> serialize_ring_element(const GRContext& ctx,
                                                 const GRElem& x);

GRElem deserialize_ring_element(const GRContext& ctx,
                                std::span<const std::uint8_t> bytes);

}  // namespace swgr::algebra

#endif  // SWGR_ALGEBRA_GR_SERIALIZATION_HPP_
