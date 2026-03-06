#include "algebra/gr_serialization.hpp"

namespace swgr::algebra {

std::vector<std::uint8_t> serialize_ring_element(const GRContext& ctx,
                                                 const GRElem& x) {
  return ctx.serialize(x);
}

GRElem deserialize_ring_element(const GRContext& ctx,
                                std::span<const std::uint8_t> bytes) {
  return ctx.deserialize(bytes);
}

}  // namespace swgr::algebra
