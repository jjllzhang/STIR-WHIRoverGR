#include "fri/common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace swgr::fri {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes,
                      std::uint64_t seed = kFnvOffset) {
  std::uint64_t hash = seed;
  for (const auto byte : bytes) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= kFnvPrime;
  }
  return hash;
}

std::uint64_t splitmix64(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

std::array<std::uint8_t, sizeof(std::uint64_t)> to_bytes(std::uint64_t value) {
  std::array<std::uint8_t, sizeof(std::uint64_t)> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
  }
  return out;
}

void append_bytes(std::vector<std::uint8_t>& dst,
                  std::span<const std::uint8_t> src) {
  dst.insert(dst.end(), src.begin(), src.end());
}

std::vector<std::uint8_t> expand_seed(std::uint64_t seed,
                                      std::size_t byte_count) {
  std::vector<std::uint8_t> out(byte_count, 0);
  std::uint64_t state = seed;
  for (std::size_t offset = 0; offset < byte_count;
       offset += sizeof(std::uint64_t)) {
    const auto word = splitmix64(state);
    const auto word_bytes = to_bytes(word);
    const std::size_t chunk =
        std::min<std::size_t>(sizeof(std::uint64_t), byte_count - offset);
    for (std::size_t i = 0; i < chunk; ++i) {
      out[offset + i] = word_bytes[i];
    }
  }
  return out;
}

}  // namespace

std::size_t folding_round_count(const FriInstance& instance,
                                std::uint64_t fold_factor,
                                std::uint64_t stop_degree) {
  if (fold_factor < 2) {
    throw std::invalid_argument("folding_round_count requires fold_factor >= 2");
  }

  std::size_t rounds = 0;
  std::uint64_t current_degree = instance.claimed_degree;
  std::uint64_t current_domain_size = instance.domain.size();
  while (current_degree > stop_degree) {
    if (current_domain_size % fold_factor != 0) {
      throw std::invalid_argument(
          "folding_round_count requires fold_factor dividing each round domain");
    }
    current_degree /= fold_factor;
    current_domain_size /= fold_factor;
    ++rounds;
  }
  return rounds;
}

std::vector<std::uint64_t> query_schedule(
    std::size_t folding_rounds, const std::vector<std::uint64_t>& configured) {
  std::vector<std::uint64_t> schedule(folding_rounds, 1);
  if (configured.empty()) {
    return schedule;
  }
  for (std::size_t i = 0; i < folding_rounds; ++i) {
    schedule[i] = configured[std::min(i, configured.size() - 1)];
  }
  return schedule;
}

std::vector<std::uint8_t> commit_oracle(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(oracle_evals.size() * ctx.elem_bytes());
  for (const auto& value : oracle_evals) {
    const auto serialized = ctx.serialize(value);
    append_bytes(bytes, serialized);
  }

  const std::uint64_t seed =
      fnv1a64(bytes) ^ (oracle_evals.size() * kFnvPrime);
  return expand_seed(seed, 32);
}

swgr::algebra::GRElem derive_round_challenge(
    const swgr::algebra::GRContext& ctx,
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index) {
  auto seed_bytes = oracle_commitment;
  const auto round_bytes = to_bytes(round_index);
  append_bytes(seed_bytes, round_bytes);
  const std::uint64_t seed = fnv1a64(seed_bytes);
  return ctx.deserialize(expand_seed(seed, ctx.elem_bytes()));
}

std::vector<std::uint64_t> derive_query_positions(
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index, std::uint64_t modulus,
    std::uint64_t query_count) {
  if (modulus == 0 || query_count == 0) {
    return {};
  }

  auto seed_bytes = oracle_commitment;
  const auto round_bytes = to_bytes(round_index);
  append_bytes(seed_bytes, round_bytes);
  std::uint64_t seed = fnv1a64(seed_bytes);

  const std::uint64_t bounded_query_count = std::min(query_count, modulus);
  std::vector<std::uint64_t> positions;
  positions.reserve(static_cast<std::size_t>(bounded_query_count));
  for (std::uint64_t i = 0; i < bounded_query_count; ++i) {
    positions.push_back(splitmix64(seed) % modulus);
  }
  return positions;
}

std::string estimate_breakdown_json(
    const std::vector<std::string>& round_entries,
    std::uint64_t final_polynomial_bytes) {
  std::string json = "{\"rounds\":[";
  for (std::size_t i = 0; i < round_entries.size(); ++i) {
    if (i != 0) {
      json += ",";
    }
    json += round_entries[i];
  }
  json += "],\"final_polynomial_bytes\":" + std::to_string(final_polynomial_bytes) +
          "}";
  return json;
}

}  // namespace swgr::fri
