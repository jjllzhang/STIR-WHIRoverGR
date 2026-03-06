#ifndef SWGR_STIR_COMMON_HPP_
#define SWGR_STIR_COMMON_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "domain.hpp"
#include "ldt.hpp"
#include "poly_utils/polynomial.hpp"
#include "stir/parameters.hpp"

namespace swgr::stir {

struct StirInstance {
  Domain domain;
  std::uint64_t claimed_degree = 0;
};

struct StirRoundState {
  std::uint64_t round_index = 0;
  std::uint64_t input_domain_size = 0;
  std::uint64_t folded_domain_size = 0;
  std::uint64_t shift_domain_size = 0;
  std::uint64_t input_degree_bound = 0;
  std::uint64_t folded_degree_bound = 0;
  std::uint64_t virtual_fold_factor = 9;
  std::uint64_t shift_power = 3;
};

struct StirRoundProof {
  std::uint64_t round_index = 0;
  std::uint64_t input_domain_size = 0;
  std::uint64_t folded_domain_size = 0;
  std::uint64_t shift_domain_size = 0;
  std::uint64_t input_degree_bound = 0;
  std::uint64_t folded_degree_bound = 0;
  swgr::algebra::GRElem folding_alpha;
  swgr::algebra::GRElem comb_randomness;
  swgr::poly_utils::Polynomial input_polynomial;
  swgr::poly_utils::Polynomial folded_polynomial;
  std::vector<swgr::algebra::GRElem> shifted_oracle_evals;
  std::vector<std::uint64_t> fold_query_positions;
  std::vector<std::uint64_t> shift_query_positions;
  std::vector<swgr::algebra::GRElem> shift_query_answers;
  swgr::crypto::MerkleProof input_oracle_proof;
  swgr::crypto::MerkleProof shift_oracle_proof;
  std::vector<swgr::algebra::GRElem> ood_points;
  std::vector<swgr::algebra::GRElem> ood_answers;
  swgr::poly_utils::Polynomial answer_polynomial;
  swgr::poly_utils::Polynomial vanishing_polynomial;
  swgr::poly_utils::Polynomial quotient_polynomial;
  swgr::poly_utils::Polynomial next_polynomial;
};

struct StirProof {
  std::vector<StirRoundProof> rounds;
  swgr::poly_utils::Polynomial final_polynomial;
  std::vector<std::vector<std::uint8_t>> oracle_roots;
  swgr::ProofStatistics stats;
};

std::uint64_t folded_degree_bound(std::uint64_t degree_bound,
                                  std::uint64_t fold_factor);

std::size_t folding_round_count(const StirInstance& instance,
                                const StirParameters& params);

std::vector<std::uint64_t> derive_unique_positions(
    const std::vector<std::uint8_t>& seed_material, std::uint64_t round_tag,
    std::uint64_t modulus, std::uint64_t requested_count);

std::vector<std::uint64_t> derive_unique_positions(
    swgr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t requested_count);

std::vector<swgr::algebra::GRElem> derive_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    const std::vector<std::uint8_t>& oracle_commitment, std::uint64_t round_tag,
    std::uint64_t sample_count);

std::vector<swgr::algebra::GRElem> derive_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain, swgr::crypto::Transcript& transcript,
    std::string_view label_prefix, std::uint64_t sample_count);

}  // namespace swgr::stir

#endif  // SWGR_STIR_COMMON_HPP_
