#include "crypto/hash.hpp"

#include "blake3.h"

#include <cstdint>
#include <span>
#include <string>
#include <stdexcept>
#include <vector>

namespace stir_whir_gr::crypto {
namespace {

constexpr HashBackend kSelectedHashBackend = HashBackend::Blake3;

std::vector<std::uint8_t> ComputeBlake3(std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> out(BLAKE3_OUT_LEN, 0);
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, data.data(), data.size());
  blake3_hasher_finalize(&hasher, out.data(), out.size());
  return out;
}

}  // namespace

std::string to_string(HashBackend backend) {
  switch (backend) {
    case HashBackend::Blake3:
      return "blake3";
  }
  return "unknown";
}

HashBackend selected_hash_backend() { return kSelectedHashBackend; }

std::size_t digest_bytes(HashBackend backend) {
  switch (backend) {
    case HashBackend::Blake3:
      return BLAKE3_OUT_LEN;
  }
  throw std::invalid_argument("unknown HashBackend");
}

std::vector<std::uint8_t> hash_bytes(HashBackend backend,
                                     std::span<const std::uint8_t> data) {
  switch (backend) {
    case HashBackend::Blake3:
      return ComputeBlake3(data);
  }
  throw std::invalid_argument("unknown HashBackend");
}

std::size_t digest_bytes(HashProfile profile) {
  return digest_bytes(profile, HashRole::Merkle);
}

std::size_t digest_bytes(HashProfile profile, HashRole role) {
  (void)profile;
  (void)role;
  return digest_bytes(selected_hash_backend());
}

std::vector<std::uint8_t> hash_bytes(HashProfile profile, HashRole role,
                                     std::span<const std::uint8_t> data) {
  (void)profile;
  (void)role;
  return hash_bytes(selected_hash_backend(), data);
}

std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     std::span<const std::uint8_t> data) {
  return hash_bytes(profile, HashRole::Transcript, data);
}

std::vector<std::uint8_t> hash_bytes(HashProfile profile,
                                     const std::vector<std::uint8_t>& data) {
  return hash_bytes(profile, std::span<const std::uint8_t>(data));
}

}  // namespace stir_whir_gr::crypto
