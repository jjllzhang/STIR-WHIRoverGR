#ifndef SWGR_CRYPTO_HASH_HPP_
#define SWGR_CRYPTO_HASH_HPP_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "parameters.hpp"

namespace swgr::crypto {

enum class HashRole {
  Transcript,
  Merkle,
};

std::size_t digest_bytes(HashProfile profile);
std::size_t digest_bytes(HashProfile profile, HashRole role);

std::vector<std::uint8_t> hash_bytes(HashProfile profile, HashRole role,
                                     std::span<const std::uint8_t> data);
std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     std::span<const std::uint8_t> data);

std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     const std::vector<std::uint8_t>& data);

}  // namespace swgr::crypto

#endif  // SWGR_CRYPTO_HASH_HPP_
