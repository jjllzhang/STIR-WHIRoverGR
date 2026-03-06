#ifndef SWGR_CRYPTO_FS_TRANSCRIPT_HPP_
#define SWGR_CRYPTO_FS_TRANSCRIPT_HPP_

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "algebra/gr_context.hpp"
#include "parameters.hpp"

namespace swgr::crypto {

class Transcript {
 public:
  explicit Transcript(HashProfile profile = HashProfile::STIR_NATIVE);

  void absorb_bytes(std::span<const std::uint8_t> data);
  void absorb_ring(const algebra::GRContext& ctx, const algebra::GRElem& x);
  algebra::GRElem challenge_ring(const algebra::GRContext& ctx,
                                 std::string_view label) const;
  std::uint64_t challenge_index(std::string_view label,
                                std::uint64_t modulus) const;

  const std::vector<std::uint8_t>& state() const { return state_; }

 private:
  HashProfile profile_;
  std::vector<std::uint8_t> state_;
};

}  // namespace swgr::crypto

#endif  // SWGR_CRYPTO_FS_TRANSCRIPT_HPP_
