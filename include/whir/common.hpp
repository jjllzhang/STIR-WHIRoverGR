#ifndef SWGR_WHIR_COMMON_HPP_
#define SWGR_WHIR_COMMON_HPP_

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "algebra/gr_context.hpp"
#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "domain.hpp"
#include "ldt.hpp"
#include "../parameters.hpp"
#include "whir/multiquadratic.hpp"

namespace swgr::whir {

struct WhirPublicParameters {
  std::shared_ptr<const swgr::algebra::GRContext> ctx;
  Domain initial_domain;
  std::uint64_t variable_count = 0;
  std::vector<std::uint64_t> layer_widths;
  std::vector<std::uint64_t> shift_repetitions;
  std::uint64_t final_repetitions = 0;
  std::vector<std::uint64_t> degree_bounds;
  std::vector<long double> deltas;
  swgr::algebra::GRElem omega;
  std::array<swgr::algebra::GRElem, 3> ternary_grid;
  std::uint64_t lambda_target = 128;
  swgr::HashProfile hash_profile = swgr::HashProfile::WHIR_NATIVE;
};

struct WhirCommitment {
  WhirPublicParameters public_params;
  std::vector<std::uint8_t> oracle_root;
  swgr::ProofStatistics stats;
};

struct WhirCommitmentState {
  std::optional<WhirPublicParameters> public_params;
  std::optional<MultiQuadraticPolynomial> polynomial;
  std::vector<swgr::algebra::GRElem> initial_oracle;
  std::vector<std::uint8_t> oracle_root;
};

struct WhirSumcheckPolynomial {
  std::vector<swgr::algebra::GRElem> coefficients;
};

struct WhirRoundProof {
  std::vector<WhirSumcheckPolynomial> sumcheck_polynomials;
  std::vector<std::uint8_t> g_root;
  swgr::crypto::MerkleProof virtual_fold_openings;
};

struct WhirProof {
  std::vector<WhirRoundProof> rounds;
  swgr::algebra::GRElem final_constant;
  swgr::crypto::MerkleProof final_openings;
  swgr::ProofStatistics stats;
};

struct WhirOpening {
  swgr::algebra::GRElem value;
  WhirProof proof;
};

inline constexpr std::string_view kTranscriptLabelPublicParameters =
    "whir.public_parameters";
inline constexpr std::string_view kTranscriptLabelCommitment =
    "whir.commitment";
inline constexpr std::string_view kTranscriptLabelOpenPoint =
    "whir.open_point";
inline constexpr std::string_view kTranscriptLabelOpenValue =
    "whir.open_value";
inline constexpr std::string_view kTranscriptLabelSumcheckPolynomial =
    "whir.sumcheck_polynomial";
inline constexpr std::string_view kTranscriptLabelAlpha = "whir.alpha";
inline constexpr std::string_view kTranscriptLabelGRoot = "whir.g_root";
inline constexpr std::string_view kTranscriptLabelShift = "whir.shift";
inline constexpr std::string_view kTranscriptLabelGamma = "whir.gamma";
inline constexpr std::string_view kTranscriptLabelFinalConstant =
    "whir.final_constant";
inline constexpr std::string_view kTranscriptLabelFinalQuery =
    "whir.final_query";

std::string indexed_label(std::string_view prefix, std::uint64_t round);

std::string indexed_label(std::string_view prefix, std::uint64_t round,
                          std::uint64_t index);

std::uint64_t serialized_message_bytes(const WhirCommitment& commitment);

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const WhirProof& proof);

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const WhirOpening& opening);

bool proof_shape_valid(const WhirProof& proof);

void absorb_public_parameters(swgr::crypto::Transcript& transcript,
                              const WhirPublicParameters& pp);

void absorb_opening_preamble(swgr::crypto::Transcript& transcript,
                             const WhirCommitment& commitment,
                             std::span<const swgr::algebra::GRElem> point,
                             const swgr::algebra::GRElem& value);

void absorb_sumcheck_polynomial(swgr::crypto::Transcript& transcript,
                                const swgr::algebra::GRContext& ctx,
                                const WhirSumcheckPolynomial& polynomial);

std::vector<std::uint64_t> derive_unique_positions(
    swgr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count);

std::vector<std::vector<std::uint8_t>> build_oracle_leaves(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals);

swgr::crypto::MerkleTree build_oracle_tree(
    swgr::HashProfile profile, const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals);

}  // namespace swgr::whir

#endif  // SWGR_WHIR_COMMON_HPP_
