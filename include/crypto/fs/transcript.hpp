#ifndef STIR_WHIR_GR_CRYPTO_FS_TRANSCRIPT_HPP_
#define STIR_WHIR_GR_CRYPTO_FS_TRANSCRIPT_HPP_

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "algebra/gr_context.hpp"
#include "parameters.hpp"

namespace stir_whir_gr::crypto {

class Transcript {
 public:
  explicit Transcript(HashProfile profile = HashProfile::STIR_NATIVE);

  void absorb_bytes(std::span<const std::uint8_t> data);
  void absorb_labeled_bytes(std::string_view label,
                            std::span<const std::uint8_t> data);
  void absorb_ring(const algebra::GRContext& ctx, const algebra::GRElem& x);
  void absorb_labeled_ring(std::string_view label,
                           const algebra::GRContext& ctx,
                           const algebra::GRElem& x);
  algebra::GRElem challenge_ring(const algebra::GRContext& ctx,
                                 std::string_view label);
  algebra::GRElem challenge_teichmuller(const algebra::GRContext& ctx,
                                        std::string_view label);
  std::uint64_t challenge_index(std::string_view label,
                                std::uint64_t modulus);

  const std::vector<std::uint8_t>& state() const { return state_; }

 private:
  std::vector<std::uint8_t> squeeze_bytes(std::string_view label,
                                          std::size_t byte_count);

  HashProfile profile_;
  std::vector<std::uint8_t> state_;
  std::uint64_t squeeze_counter_ = 0;
};

}  // namespace stir_whir_gr::crypto

#endif  // STIR_WHIR_GR_CRYPTO_FS_TRANSCRIPT_HPP_
