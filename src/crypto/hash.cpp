#include "crypto/hash.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "utils.hpp"

namespace swgr::crypto {
namespace {

std::uint64_t profile_seed(HashProfile profile) {
  switch (profile) {
    case HashProfile::STIR_NATIVE:
      return 0x535449524E415449ULL;
    case HashProfile::WHIR_NATIVE:
      return 0x574849524E415449ULL;
  }
  return 0x9E3779B97F4A7C15ULL;
}

std::uint64_t splitmix64(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

void mix_bytes(std::uint64_t& state, const std::vector<std::uint8_t>& data) {
  for (const auto byte : data) {
    state ^= static_cast<std::uint64_t>(byte) + 0x9E3779B97F4A7C15ULL +
             (state << 6U) + (state >> 2U);
  }
}

}  // namespace

std::size_t digest_bytes(HashProfile profile) {
  switch (profile) {
    case HashProfile::STIR_NATIVE:
    case HashProfile::WHIR_NATIVE:
      return 32;
  }
  return 32;
}

std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     const std::vector<std::uint8_t>& data) {
  const std::size_t digest_size = digest_bytes(profile);
  std::vector<std::uint8_t> out(digest_size, 0);

  std::uint64_t state = profile_seed(profile);
  mix_bytes(state, data);
  state ^= static_cast<std::uint64_t>(data.size()) * 0xA24BAED4963EE407ULL;

  for (std::size_t offset = 0; offset < digest_size; offset += sizeof(std::uint64_t)) {
    const std::uint64_t word = splitmix64(state);
    const std::size_t remaining = digest_size - offset;
    const std::size_t chunk =
        remaining < sizeof(std::uint64_t) ? remaining : sizeof(std::uint64_t);
    for (std::size_t i = 0; i < chunk; ++i) {
      out[offset + i] =
          static_cast<std::uint8_t>((word >> (8U * i)) & 0xFFU);
    }
  }

  return out;
}

}  // namespace swgr::crypto
