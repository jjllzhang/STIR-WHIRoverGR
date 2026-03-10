#ifndef SWGR_FRI_COMMON_HPP_
#define SWGR_FRI_COMMON_HPP_

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

namespace swgr::fri {

struct FriInstance {
  Domain domain;
  std::uint64_t claimed_degree = 0;
};

struct FriCommitment {
  Domain domain;
  std::uint64_t degree_bound = 0;
  std::vector<std::uint8_t> oracle_root;
  swgr::ProofStatistics stats;
};

struct FriOpeningClaim {
  swgr::algebra::GRElem alpha;
  swgr::algebra::GRElem value;
};

struct FriRoundProof {
  swgr::crypto::MerkleProof oracle_proof;
};

struct FriProof {
  std::vector<FriRoundProof> rounds;
  swgr::poly_utils::Polynomial final_polynomial;
  std::vector<std::vector<std::uint8_t>> oracle_roots;
  swgr::ProofStatistics stats;
};

struct FriRoundWitness {
  std::vector<swgr::algebra::GRElem> oracle_evals;
};

struct FriWitness {
  std::vector<FriRoundWitness> rounds;
};

struct FriProofWithWitness {
  FriProof proof;
  FriWitness witness;
};

struct FriOpeningProof {
  swgr::crypto::MerkleProof committed_oracle_proof;
  FriProof quotient_proof;
  swgr::ProofStatistics stats;
};

struct FriOpeningWitness {
  std::vector<swgr::algebra::GRElem> committed_oracle_evals;
  FriWitness quotient_witness;
};

struct FriOpening {
  FriOpeningClaim claim;
  FriOpeningProof proof;
};

struct FriOpeningArtifact {
  FriOpening opening;
  FriOpeningWitness witness;
};

std::uint64_t serialized_message_bytes(const FriCommitment& commitment);

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const FriProof& proof);

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const FriOpeningProof& proof);

// Counts only the prover reply bytes for the opening message: value plus proof.
std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const FriOpening& opening);

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

// Current prototype PCS path reconstructs the round-0 virtual oracle directly
// from sparse openings of committed `f|L`, so it only accepts `alpha in T \ L`.
// Phase 0 of the soundness-repair plan freezes a later theorem-facing target of
// `alpha <- T`, with `alpha in L` handled by a distinct upgraded PCS path.
bool opening_point_valid(const FriCommitment& commitment,
                         const swgr::algebra::GRElem& alpha);

// This helper implements the current sparse-opening shortcut, not the final
// theorem-facing `pi_FRICom` semantics for `alpha <- T`.
std::vector<swgr::algebra::GRElem> build_virtual_oracle(
    const Domain& domain, std::span<const swgr::algebra::GRElem> oracle_evals,
    const swgr::algebra::GRElem& alpha,
    const swgr::algebra::GRElem& value);

inline std::vector<swgr::algebra::GRElem> adapt_virtual_oracle(
    const Domain& domain, std::span<const swgr::algebra::GRElem> oracle_evals,
    const swgr::algebra::GRElem& alpha,
    const swgr::algebra::GRElem& value) {
  return build_virtual_oracle(domain, oracle_evals, alpha, value);
}

std::vector<std::uint64_t> query_schedule(
    std::size_t folding_rounds, const std::vector<std::uint64_t>& configured);

std::vector<std::uint8_t> commit_oracle(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals);

std::vector<std::vector<std::uint8_t>> build_oracle_leaves(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size);

std::vector<std::uint8_t> serialize_oracle_bundle(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size, std::uint64_t bundle_index);

std::vector<swgr::algebra::GRElem> deserialize_oracle_bundle(
    const swgr::algebra::GRContext& ctx, std::span<const std::uint8_t> bytes);

swgr::crypto::MerkleTree build_oracle_tree(
    swgr::HashProfile profile, const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals,
    std::uint64_t bundle_size);

swgr::algebra::GRElem derive_round_challenge(
    const swgr::algebra::GRContext& ctx,
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index);

swgr::algebra::GRElem derive_round_challenge(
    swgr::crypto::Transcript& transcript, const swgr::algebra::GRContext& ctx,
    std::string_view label);

// Paper-facing FRI folding challenges are sampled from the maximal
// Teichmuller exceptional set `T`, not from the ambient ring.
swgr::algebra::GRElem derive_fri_folding_challenge(
    swgr::crypto::Transcript& transcript, const swgr::algebra::GRContext& ctx,
    std::string_view label);

std::vector<std::uint64_t> derive_query_positions(
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index, std::uint64_t modulus,
    std::uint64_t query_count);

std::vector<std::uint64_t> derive_query_positions(
    swgr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count);

std::vector<std::uint64_t> derive_unique_query_positions(
    swgr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count);

}  // namespace swgr::fri

#endif  // SWGR_FRI_COMMON_HPP_
