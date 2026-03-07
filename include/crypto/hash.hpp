#ifndef SWGR_CRYPTO_HASH_HPP_
#define SWGR_CRYPTO_HASH_HPP_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "parameters.hpp"

namespace swgr::crypto {

enum class HashBackend {
  Blake3,
};

enum class HashRole {
  Transcript,
  Merkle,
};

std::string to_string(HashBackend backend);
HashBackend selected_hash_backend();

std::size_t digest_bytes(HashBackend backend);
std::vector<std::uint8_t> hash_bytes(HashBackend backend,
                                     std::span<const std::uint8_t> data);

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
