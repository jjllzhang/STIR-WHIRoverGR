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

struct FriRoundState {
  std::uint64_t round_index = 0;
  std::uint64_t fold_factor = 0;
};

struct FriRoundProof {
  std::uint64_t round_index = 0;
  std::uint64_t domain_size = 0;
  swgr::algebra::GRElem folding_alpha;
  std::vector<std::uint64_t> query_positions;
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

std::size_t folding_round_count(const FriInstance& instance,
                                std::uint64_t fold_factor,
                                std::uint64_t stop_degree);

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
