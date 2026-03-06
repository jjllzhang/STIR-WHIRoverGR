#include "crypto/fs/transcript.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <stdexcept>

#include "algebra/gr_serialization.hpp"
#include "crypto/hash.hpp"

namespace swgr::crypto {
namespace {

std::vector<std::uint8_t> make_labelled_input(
    std::span<const std::uint8_t> state, std::string_view label,
    std::uint8_t tag) {
  std::vector<std::uint8_t> input;
  input.reserve(state.size() + label.size() + 2);
  input.insert(input.end(), state.begin(), state.end());
  input.push_back(tag);
  input.insert(input.end(), label.begin(), label.end());
  return input;
}

std::uint64_t read_u64_le(std::span<const std::uint8_t> bytes) {
  std::uint64_t value = 0;
  const std::size_t width =
      std::min<std::size_t>(bytes.size(), sizeof(std::uint64_t));
  for (std::size_t i = 0; i < width; ++i) {
    value |= static_cast<std::uint64_t>(bytes[i]) << (8U * i);
  }
  return value;
}

}  // namespace

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
  const std::size_t elem_bytes = ctx.elem_bytes();
  std::vector<std::uint8_t> bytes;
  bytes.reserve(elem_bytes);

  std::vector<std::uint8_t> seed =
      make_labelled_input(state_, label, static_cast<std::uint8_t>(0xA1));
  std::uint64_t counter = 0;
  while (bytes.size() < elem_bytes) {
    std::vector<std::uint8_t> block_input = seed;
    for (std::size_t i = 0; i < sizeof(counter); ++i) {
      block_input.push_back(
          static_cast<std::uint8_t>((counter >> (8U * i)) & 0xFFU));
    }
    const auto block = hash_bytes(profile_, block_input);
    const std::size_t to_copy =
        std::min(block.size(), elem_bytes - bytes.size());
    bytes.insert(bytes.end(), block.begin(), block.begin() + to_copy);
    ++counter;
  }

  return algebra::deserialize_ring_element(ctx, bytes);
}

std::uint64_t Transcript::challenge_index(std::string_view label,
                                          std::uint64_t modulus) const {
  if (modulus == 0) {
    throw std::invalid_argument("challenge_index requires modulus > 0");
  }
  const auto input =
      make_labelled_input(state_, label, static_cast<std::uint8_t>(0xB2));
  const auto digest = hash_bytes(profile_, input);
  return read_u64_le(digest) % modulus;
}

}  // namespace swgr::crypto
