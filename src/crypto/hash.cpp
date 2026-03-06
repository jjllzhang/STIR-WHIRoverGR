#include "crypto/hash.hpp"

#include "utils.hpp"

namespace swgr::crypto {

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
  (void)profile;
  (void)data;
  throw_unimplemented("crypto::hash_bytes");
}

}  // namespace swgr::crypto
