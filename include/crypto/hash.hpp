#ifndef SWGR_CRYPTO_HASH_HPP_
#define SWGR_CRYPTO_HASH_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "parameters.hpp"

namespace swgr::crypto {

std::size_t digest_bytes(HashProfile profile);

std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     const std::vector<std::uint8_t>& data);

}  // namespace swgr::crypto

#endif  // SWGR_CRYPTO_HASH_HPP_
