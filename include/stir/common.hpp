#ifndef STIR_WHIR_GR_STIR_COMMON_HPP_
#define STIR_WHIR_GR_STIR_COMMON_HPP_

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
#include "stir/parameters.hpp"

namespace stir_whir_gr::stir {

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
  std::vector<std::uint8_t> g_root;
  std::vector<stir_whir_gr::algebra::GRElem> betas;
  stir_whir_gr::poly_utils::Polynomial ans_polynomial;
  stir_whir_gr::crypto::MerkleProof queries_to_prev;
  stir_whir_gr::poly_utils::Polynomial shake_polynomial;
};

struct StirRoundWitness {
  stir_whir_gr::poly_utils::Polynomial input_polynomial;
  stir_whir_gr::poly_utils::Polynomial folded_polynomial;
  std::vector<stir_whir_gr::algebra::GRElem> shifted_oracle_evals;
  stir_whir_gr::poly_utils::Polynomial answer_polynomial;
  stir_whir_gr::poly_utils::Polynomial vanishing_polynomial;
  stir_whir_gr::poly_utils::Polynomial quotient_polynomial;
  stir_whir_gr::poly_utils::Polynomial next_polynomial;
};

struct StirProof {
  std::vector<std::uint8_t> initial_root;
  std::vector<StirRoundProof> rounds;
  stir_whir_gr::poly_utils::Polynomial final_polynomial;
  stir_whir_gr::crypto::MerkleProof queries_to_final;
  stir_whir_gr::ProofStatistics stats;
};

struct StirWitness {
  std::vector<StirRoundWitness> rounds;
};

struct StirProofWithWitness {
  StirProof proof;
  StirWitness witness;
};

std::uint64_t serialized_message_bytes(
    const stir_whir_gr::algebra::GRContext& ctx, const StirProof& proof);

bool points_have_unit_differences(
    const Domain& domain, std::span<const stir_whir_gr::algebra::GRElem> points);

bool domains_have_unit_differences(const Domain& lhs, const Domain& rhs);

stir_whir_gr::algebra::GRElem derive_stir_folding_challenge(
    stir_whir_gr::crypto::Transcript& transcript, const stir_whir_gr::algebra::GRContext& ctx,
    std::string_view label);

stir_whir_gr::algebra::GRElem derive_stir_comb_challenge(
    stir_whir_gr::crypto::Transcript& transcript, const stir_whir_gr::algebra::GRContext& ctx,
    std::string_view label);

bool domain_is_subset_of_teichmuller_units(const Domain& domain);

bool theorem_ood_pool_has_capacity(const Domain& input_domain,
                                   const Domain& shift_domain,
                                   const Domain& folded_domain,
                                   std::uint64_t required_points);

bool theorem_shake_pool_has_capacity(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    std::span<const stir_whir_gr::algebra::GRElem> quotient_points);

std::uint64_t folded_degree_bound(std::uint64_t degree_bound,
                                  std::uint64_t fold_factor);

std::size_t folding_round_count(const StirInstance& instance,
                                const StirParameters& params);

std::vector<std::uint64_t> derive_unique_positions(
    const std::vector<std::uint8_t>& seed_material, std::uint64_t round_tag,
    std::uint64_t modulus, std::uint64_t requested_count);

std::vector<std::uint64_t> derive_unique_positions(
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t requested_count);

std::vector<stir_whir_gr::algebra::GRElem> derive_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    const std::vector<std::uint8_t>& oracle_commitment, std::uint64_t round_tag,
    std::uint64_t sample_count);

std::vector<stir_whir_gr::algebra::GRElem> derive_ood_points(
    const StirParameters& params, const Domain& input_domain,
    const Domain& shift_domain, const Domain& folded_domain,
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t sample_count);

std::vector<stir_whir_gr::algebra::GRElem> derive_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain, stir_whir_gr::crypto::Transcript& transcript,
    std::string_view label_prefix, std::uint64_t sample_count);

std::vector<stir_whir_gr::algebra::GRElem> derive_theorem_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain, stir_whir_gr::crypto::Transcript& transcript,
    std::string_view label_prefix, std::uint64_t sample_count);

stir_whir_gr::algebra::GRElem derive_shake_point(
    const StirParameters& params, const Domain& input_domain,
    const Domain& shift_domain, const Domain& folded_domain,
    const std::vector<stir_whir_gr::algebra::GRElem>& quotient_points,
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix);

stir_whir_gr::algebra::GRElem derive_shake_point(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    const std::vector<stir_whir_gr::algebra::GRElem>& quotient_points,
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix);

stir_whir_gr::algebra::GRElem derive_theorem_shake_point(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    const std::vector<stir_whir_gr::algebra::GRElem>& quotient_points,
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix);

bool try_reuse_next_round_input_oracle(
    const Domain& domain,
    const std::vector<stir_whir_gr::algebra::GRElem>& shifted_oracle_evals,
    const stir_whir_gr::poly_utils::Polynomial& answer_polynomial,
    const stir_whir_gr::poly_utils::Polynomial& vanishing_polynomial,
    const stir_whir_gr::poly_utils::Polynomial& quotient_polynomial,
    const stir_whir_gr::algebra::GRElem& comb_randomness,
    std::uint64_t target_degree_bound, std::uint64_t current_degree_bound,
    std::vector<stir_whir_gr::algebra::GRElem>* next_oracle_evals);

}  // namespace stir_whir_gr::stir

#endif  // STIR_WHIR_GR_STIR_COMMON_HPP_
