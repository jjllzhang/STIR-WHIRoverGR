#include "fri/common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "algebra/teichmuller.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"

namespace stir_whir_gr::fri {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kParallelOracleLeafThreshold = 128;

long CheckedLong(std::uint64_t value, const char* label) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

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

template <typename Sink>
void SerializeRingElement(Sink& sink, const stir_whir_gr::algebra::GRContext& ctx,
                          const stir_whir_gr::algebra::GRElem& value) {
  stir_whir_gr::SerializeBytes(sink, ctx.serialize(value));
}

template <typename Sink>
void SerializeRingVector(Sink& sink, const stir_whir_gr::algebra::GRContext& ctx,
                         std::span<const stir_whir_gr::algebra::GRElem> values) {
  stir_whir_gr::SerializeUint64(sink, static_cast<std::uint64_t>(values.size()));
  for (const auto& value : values) {
    SerializeRingElement(sink, ctx, value);
  }
}

template <typename Sink>
void SerializeMerkleProof(Sink& sink, const stir_whir_gr::crypto::MerkleProof& proof) {
  stir_whir_gr::SerializeUint64Vector(sink, proof.queried_indices);
  stir_whir_gr::SerializeByteVector(sink, proof.leaf_payloads);
  stir_whir_gr::SerializeByteVector(sink, proof.sibling_hashes);
}

template <typename Sink>
void SerializeFriProofBody(Sink& sink, const stir_whir_gr::algebra::GRContext& ctx,
                           const FriProof& proof) {
  stir_whir_gr::SerializeUint64(sink,
                        static_cast<std::uint64_t>(proof.rounds.size()));
  for (const auto& round : proof.rounds) {
    SerializeMerkleProof(sink, round.parent_oracle_proof);
    SerializeMerkleProof(sink, round.child_oracle_proof);
  }
  stir_whir_gr::SerializeByteVector(sink, proof.oracle_roots);
  SerializeRingVector(sink, ctx, proof.final_oracle);
  SerializeRingVector(sink, ctx, proof.revealed_committed_oracle);
}

void append_serialized_in_current_ntl_context(
    const stir_whir_gr::algebra::GRContext& ctx, const stir_whir_gr::algebra::GRElem& value,
    std::vector<std::uint8_t>& out) {
  const std::size_t width = ctx.coeff_bytes();
  const long r = CheckedLong(ctx.config().r, "r");
  const std::size_t base_offset = out.size();
  out.resize(base_offset + ctx.elem_bytes(), 0);

  const NTL::ZZ_pX poly = NTL::rep(value);
  for (long i = 0; i < r; ++i) {
    NTL::ZZ coeff_value = NTL::rep(NTL::coeff(poly, i));
    coeff_value %= ctx.modulus();
    if (coeff_value < 0) {
      coeff_value += ctx.modulus();
    }
    NTL::BytesFromZZ(reinterpret_cast<unsigned char*>(out.data()) + base_offset +
                        static_cast<std::size_t>(i) * width,
                    coeff_value, static_cast<long>(width));
  }
}

std::vector<std::uint8_t> bundle_payload(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size, std::uint64_t bundle_index) {
  if (bundle_size == 0) {
    throw std::invalid_argument("bundle_payload requires bundle_size > 0");
  }
  if (oracle_evals.empty() || oracle_evals.size() % bundle_size != 0) {
    throw std::invalid_argument(
        "bundle_payload requires oracle size divisible by bundle_size");
  }

  const std::uint64_t bundle_count =
      static_cast<std::uint64_t>(oracle_evals.size()) / bundle_size;
  if (bundle_index >= bundle_count) {
    throw std::out_of_range("bundle index exceeds oracle bundle count");
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(static_cast<std::size_t>(bundle_size) * ctx.elem_bytes());
  for (std::uint64_t offset = 0; offset < bundle_size; ++offset) {
    const std::uint64_t oracle_index = bundle_index + offset * bundle_count;
    append_serialized_in_current_ntl_context(
        ctx, oracle_evals[static_cast<std::size_t>(oracle_index)], bytes);
  }
  return bytes;
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

std::uint64_t serialized_message_bytes(const FriCommitment& commitment) {
  stir_whir_gr::CountingSink sink;
  stir_whir_gr::SerializeBytes(sink, commitment.oracle_root);
  return sink.size();
}

std::uint64_t serialized_message_bytes(const stir_whir_gr::algebra::GRContext& ctx,
                                       const FriProof& proof) {
  stir_whir_gr::CountingSink sink;
  SerializeFriProofBody(sink, ctx, proof);
  return sink.size();
}

std::uint64_t serialized_message_bytes(const stir_whir_gr::algebra::GRContext& ctx,
                                       const FriOpening& opening) {
  stir_whir_gr::CountingSink sink;
  SerializeRingElement(sink, ctx, opening.claim.value);
  SerializeFriProofBody(sink, ctx, opening.proof);
  return sink.size();
}

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

std::uint64_t opening_degree_bound(std::uint64_t degree_bound) {
  return degree_bound == 0 ? 0 : degree_bound - 1U;
}

FriInstance opening_instance(const FriCommitment& commitment) {
  return FriInstance{
      .domain = commitment.domain,
      .claimed_degree = opening_degree_bound(commitment.degree_bound),
  };
}

bool commitment_domain_supported(const FriCommitment& commitment) {
  return commitment.domain.is_teichmuller_subset();
}

bool opening_point_valid(const FriCommitment& commitment,
                         const stir_whir_gr::algebra::GRElem& alpha) {
  const auto& ctx = commitment.domain.context();
  if (!commitment_domain_supported(commitment) ||
      !stir_whir_gr::algebra::is_teichmuller_element(ctx, alpha)) {
    return false;
  }
  return true;
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
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals) {
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

std::vector<std::vector<std::uint8_t>> build_oracle_leaves(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size) {
  if (bundle_size == 0) {
    throw std::invalid_argument("build_oracle_leaves requires bundle_size > 0");
  }
  if (oracle_evals.empty() || oracle_evals.size() % bundle_size != 0) {
    throw std::invalid_argument(
        "build_oracle_leaves requires oracle size divisible by bundle_size");
  }

  const std::uint64_t bundle_count =
      static_cast<std::uint64_t>(oracle_evals.size()) / bundle_size;
  std::vector<std::vector<std::uint8_t>> leaves(
      static_cast<std::size_t>(bundle_count));
  ctx.parallel_for_with_ntl_context(
      static_cast<std::ptrdiff_t>(bundle_count),
      bundle_count >= kParallelOracleLeafThreshold,
      [&](std::ptrdiff_t bundle_index) {
        leaves[static_cast<std::size_t>(bundle_index)] = bundle_payload(
            ctx, oracle_evals, bundle_size,
            static_cast<std::uint64_t>(bundle_index));
      });
  return leaves;
}

std::vector<std::uint8_t> serialize_oracle_bundle(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size, std::uint64_t bundle_index) {
  return ctx.with_ntl_context(
      [&] { return bundle_payload(ctx, oracle_evals, bundle_size, bundle_index); });
}

std::vector<stir_whir_gr::algebra::GRElem> deserialize_oracle_bundle(
    const stir_whir_gr::algebra::GRContext& ctx, std::span<const std::uint8_t> bytes) {
  const std::size_t elem_bytes = ctx.elem_bytes();
  if (elem_bytes == 0 || bytes.size() % elem_bytes != 0) {
    throw std::invalid_argument(
        "deserialize_oracle_bundle requires a whole-number element payload");
  }

  std::vector<stir_whir_gr::algebra::GRElem> values;
  values.reserve(bytes.size() / elem_bytes);
  for (std::size_t offset = 0; offset < bytes.size(); offset += elem_bytes) {
    values.push_back(
        ctx.deserialize(bytes.subspan(offset, elem_bytes)));
  }
  return values;
}

stir_whir_gr::crypto::MerkleTree build_oracle_tree(
    stir_whir_gr::HashProfile profile, const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size) {
  return stir_whir_gr::crypto::MerkleTree(
      profile, build_oracle_leaves(ctx, oracle_evals, bundle_size));
}

stir_whir_gr::algebra::GRElem derive_round_challenge(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index) {
  auto seed_bytes = oracle_commitment;
  const auto round_bytes = to_bytes(round_index);
  append_bytes(seed_bytes, round_bytes);
  const std::uint64_t seed = fnv1a64(seed_bytes);
  return ctx.deserialize(expand_seed(seed, ctx.elem_bytes()));
}

stir_whir_gr::algebra::GRElem derive_round_challenge(
    stir_whir_gr::crypto::Transcript& transcript, const stir_whir_gr::algebra::GRContext& ctx,
    std::string_view label) {
  return transcript.challenge_ring(ctx, label);
}

stir_whir_gr::algebra::GRElem derive_fri_folding_challenge(
    stir_whir_gr::crypto::Transcript& transcript, const stir_whir_gr::algebra::GRContext& ctx,
    std::string_view label) {
  return transcript.challenge_teichmuller(ctx, label);
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

std::vector<std::uint64_t> derive_query_positions(
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count) {
  if (modulus == 0 || query_count == 0) {
    return {};
  }

  std::vector<std::uint64_t> positions;
  positions.reserve(static_cast<std::size_t>(query_count));
  for (std::uint64_t i = 0; i < query_count; ++i) {
    positions.push_back(transcript.challenge_index(
        std::string(label_prefix) + ":" + std::to_string(i), modulus));
  }
  return positions;
}

std::vector<std::uint64_t> derive_unique_query_positions(
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count) {
  if (modulus == 0 || query_count == 0) {
    return {};
  }

  const std::uint64_t capped_count = std::min(query_count, modulus);
  std::vector<std::uint64_t> positions;
  positions.reserve(static_cast<std::size_t>(capped_count));
  for (std::uint64_t i = 0; positions.size() < static_cast<std::size_t>(capped_count);
       ++i) {
    const auto candidate = transcript.challenge_index(
        std::string(label_prefix) + ":" + std::to_string(i), modulus);
    if (std::find(positions.begin(), positions.end(), candidate) ==
        positions.end()) {
      positions.push_back(candidate);
    }
  }
  return positions;
}

}  // namespace stir_whir_gr::fri
