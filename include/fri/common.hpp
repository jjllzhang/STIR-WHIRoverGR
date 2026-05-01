#ifndef STIR_WHIR_GR_FRI_COMMON_HPP_
#define STIR_WHIR_GR_FRI_COMMON_HPP_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "domain.hpp"
#include "ldt.hpp"
#include "poly_utils/polynomial.hpp"

namespace stir_whir_gr::fri {

struct FriInstance {
  Domain domain;
  std::uint64_t claimed_degree = 0;
};

struct FriCommitment {
  Domain domain;
  std::uint64_t degree_bound = 0;
  std::vector<std::uint8_t> oracle_root;
  stir_whir_gr::ProofStatistics stats;
};

struct FriOpeningClaim {
  stir_whir_gr::algebra::GRElem alpha;
  stir_whir_gr::algebra::GRElem value;
};

struct FriRoundProof {
  stir_whir_gr::crypto::MerkleProof parent_oracle_proof;
  stir_whir_gr::crypto::MerkleProof child_oracle_proof;
};

struct FriProof {
  std::vector<FriRoundProof> rounds;
  std::vector<std::vector<std::uint8_t>> oracle_roots;
  std::vector<stir_whir_gr::algebra::GRElem> final_oracle;
  std::vector<stir_whir_gr::algebra::GRElem> revealed_committed_oracle;
  stir_whir_gr::ProofStatistics stats;
};

struct FriOpening {
  FriOpeningClaim claim;
  FriProof proof;
};

std::uint64_t serialized_message_bytes(const FriCommitment& commitment);

std::uint64_t serialized_message_bytes(
    const stir_whir_gr::algebra::GRContext& ctx, const FriProof& proof);

// Counts only the prover reply bytes for the opening message: value plus proof.
std::uint64_t serialized_message_bytes(
    const stir_whir_gr::algebra::GRContext& ctx, const FriOpening& opening);

std::size_t folding_round_count(const FriInstance& instance,
                                std::uint64_t fold_factor,
                                std::uint64_t stop_degree);

std::uint64_t opening_degree_bound(std::uint64_t degree_bound);

inline std::uint64_t quotient_degree_bound(std::uint64_t degree_bound) {
  return opening_degree_bound(degree_bound);
}

FriInstance opening_instance(const FriCommitment& commitment);

inline FriInstance quotient_instance(const FriCommitment& commitment) {
  return opening_instance(commitment);
}

bool commitment_domain_supported(const FriCommitment& commitment);

// The theorem-facing PCS path accepts verifier challenges `alpha <- T` across
// the full Teichmuller set.
bool opening_point_valid(const FriCommitment& commitment,
                         const stir_whir_gr::algebra::GRElem& alpha);

std::vector<std::uint64_t> query_schedule(
    std::size_t folding_rounds, const std::vector<std::uint64_t>& configured);

std::vector<std::uint8_t> commit_oracle(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals);

std::vector<std::vector<std::uint8_t>> build_oracle_leaves(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size);

std::vector<std::uint8_t> serialize_oracle_bundle(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size, std::uint64_t bundle_index);

std::vector<stir_whir_gr::algebra::GRElem> deserialize_oracle_bundle(
    const stir_whir_gr::algebra::GRContext& ctx, std::span<const std::uint8_t> bytes);

stir_whir_gr::crypto::MerkleTree build_oracle_tree(
    stir_whir_gr::HashProfile profile, const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size);

stir_whir_gr::algebra::GRElem derive_round_challenge(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index);

stir_whir_gr::algebra::GRElem derive_round_challenge(
    stir_whir_gr::crypto::Transcript& transcript, const stir_whir_gr::algebra::GRContext& ctx,
    std::string_view label);

// Paper-facing FRI folding challenges are sampled from the maximal
// Teichmuller exceptional set `T`, not from the ambient ring.
stir_whir_gr::algebra::GRElem derive_fri_folding_challenge(
    stir_whir_gr::crypto::Transcript& transcript, const stir_whir_gr::algebra::GRContext& ctx,
    std::string_view label);

std::vector<std::uint64_t> derive_query_positions(
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index, std::uint64_t modulus,
    std::uint64_t query_count);

std::vector<std::uint64_t> derive_query_positions(
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count);

std::vector<std::uint64_t> derive_unique_query_positions(
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count);

}  // namespace stir_whir_gr::fri

#endif  // STIR_WHIR_GR_FRI_COMMON_HPP_
