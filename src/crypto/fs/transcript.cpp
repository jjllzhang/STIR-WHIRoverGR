#include "crypto/fs/transcript.hpp"

#include <stdexcept>

#include "algebra/gr_serialization.hpp"
#include "utils.hpp"

namespace swgr::crypto {

Transcript::Transcript(HashProfile profile) : profile_(profile) {}

void Transcript::absorb_bytes(std::span<const std::uint8_t> data) {
  state_.insert(state_.end(), data.begin(), data.end());
}

void Transcript::absorb_ring(const algebra::GRContext& ctx,
                             const algebra::GRElem& x) {
  const auto bytes = algebra::serialize_ring_element(ctx, x);
  absorb_bytes(bytes);
}

algebra::GRElem Transcript::challenge_ring(const algebra::GRContext& ctx,
                                           std::string_view label) const {
  (void)ctx;
  (void)label;
  throw_unimplemented("crypto::Transcript::challenge_ring");
}

std::uint64_t Transcript::challenge_index(std::string_view label,
                                          std::uint64_t modulus) const {
  if (modulus == 0) {
    throw std::invalid_argument("challenge_index requires modulus > 0");
  }
  (void)label;
  throw_unimplemented("crypto::Transcript::challenge_index");
}

}  // namespace swgr::crypto
