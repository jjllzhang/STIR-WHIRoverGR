#include "crypto/hash.hpp"

#include <openssl/evp.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace swgr::crypto {
namespace {

const EVP_MD* ResolveHash(HashProfile profile, HashRole role) {
  switch (profile) {
    case HashProfile::STIR_NATIVE:
      return role == HashRole::Merkle ? EVP_sha3_256() : EVP_sha256();
    case HashProfile::WHIR_NATIVE:
      return role == HashRole::Merkle ? EVP_sha256() : EVP_sha3_256();
  }
  throw std::invalid_argument("unknown HashProfile");
}

std::vector<std::uint8_t> ComputeDigest(const EVP_MD* md,
                                        std::span<const std::uint8_t> data) {
  if (md == nullptr) {
    throw std::runtime_error("OpenSSL returned null EVP_MD");
  }

  std::vector<std::uint8_t> out(
      static_cast<std::size_t>(EVP_MD_get_size(md)), 0);
  EVP_MD_CTX* const ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) {
    throw std::runtime_error("EVP_MD_CTX_new failed");
  }

  unsigned int out_size = 0;
  const bool ok =
      EVP_DigestInit_ex(ctx, md, nullptr) == 1 &&
      EVP_DigestUpdate(ctx, data.data(), data.size()) == 1 &&
      EVP_DigestFinal_ex(ctx, out.data(), &out_size) == 1;
  EVP_MD_CTX_free(ctx);
  if (!ok || out_size != out.size()) {
    throw std::runtime_error("OpenSSL digest computation failed");
  }
  return out;
}

}  // namespace

std::size_t digest_bytes(HashProfile profile) {
  return digest_bytes(profile, HashRole::Merkle);
}

std::size_t digest_bytes(HashProfile profile, HashRole role) {
  return static_cast<std::size_t>(EVP_MD_get_size(ResolveHash(profile, role)));
}

std::vector<std::uint8_t> hash_bytes(HashProfile profile, HashRole role,
                                     std::span<const std::uint8_t> data) {
  return ComputeDigest(ResolveHash(profile, role), data);
}

std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     std::span<const std::uint8_t> data) {
  return hash_bytes(profile, HashRole::Transcript, data);
}

std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     const std::vector<std::uint8_t>& data) {
  return hash_bytes(profile, std::span<const std::uint8_t>(data));
}

}  // namespace swgr::crypto
