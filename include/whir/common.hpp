#ifndef STIR_WHIR_GR_WHIR_COMMON_HPP_
#define STIR_WHIR_GR_WHIR_COMMON_HPP_

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

namespace stir_whir_gr::whir {

struct WhirPublicParameters {
  std::shared_ptr<const stir_whir_gr::algebra::GRContext> ctx;
  Domain initial_domain;
  std::uint64_t variable_count = 0;
  std::vector<std::uint64_t> layer_widths;
  std::vector<std::uint64_t> shift_repetitions;
  std::uint64_t final_repetitions = 0;
  std::vector<std::uint64_t> degree_bounds;
  std::vector<long double> deltas;
  stir_whir_gr::algebra::GRElem omega;
  std::array<stir_whir_gr::algebra::GRElem, 3> ternary_grid;
  std::uint64_t lambda_target = 128;
  stir_whir_gr::HashProfile hash_profile = stir_whir_gr::HashProfile::WHIR_NATIVE;
};

struct WhirCommitment {
  WhirPublicParameters public_params;
  std::vector<std::uint8_t> oracle_root;
  stir_whir_gr::ProofStatistics stats;
};

struct WhirCommitmentState {
  std::optional<WhirPublicParameters> public_params;
  std::optional<MultiQuadraticPolynomial> polynomial;
  std::optional<stir_whir_gr::crypto::MerkleTree> initial_tree;
  std::vector<stir_whir_gr::algebra::GRElem> initial_oracle;
  std::vector<std::uint8_t> oracle_root;
};

struct WhirSumcheckPolynomial {
  std::vector<stir_whir_gr::algebra::GRElem> coefficients;
};

struct WhirRoundProof {
  std::vector<WhirSumcheckPolynomial> sumcheck_polynomials;
  std::vector<std::uint8_t> g_root;
  stir_whir_gr::crypto::MerkleProof virtual_fold_openings;
};

struct WhirProof {
  std::vector<WhirRoundProof> rounds;
  stir_whir_gr::algebra::GRElem final_constant;
  stir_whir_gr::crypto::MerkleProof final_openings;
  stir_whir_gr::ProofStatistics stats;
};

struct WhirOpening {
  stir_whir_gr::algebra::GRElem value;
  WhirProof proof;
};

inline constexpr std::string_view kTranscriptLabelPublicParameters =
    "whir.pp";
inline constexpr std::string_view kTranscriptLabelCommitment =
    "whir.commitment";
inline constexpr std::string_view kTranscriptLabelOpenPoint =
    "whir.open.point";
inline constexpr std::string_view kTranscriptLabelOpenValue =
    "whir.open.value";
inline constexpr std::string_view kTranscriptLabelSumcheckPolynomial =
    "whir.sumcheck.poly";
inline constexpr std::string_view kTranscriptLabelAlpha = "whir.alpha";
inline constexpr std::string_view kTranscriptLabelGRoot = "whir.g_root";
inline constexpr std::string_view kTranscriptLabelShift = "whir.shift";
inline constexpr std::string_view kTranscriptLabelGamma = "whir.gamma";
inline constexpr std::string_view kTranscriptLabelFinalConstant =
    "whir.final.constant";
inline constexpr std::string_view kTranscriptLabelFinalQuery =
    "whir.final.query";

std::string indexed_label(std::string_view prefix, std::uint64_t round);

std::string indexed_label(std::string_view prefix, std::uint64_t round,
                          std::uint64_t index);

std::uint64_t serialized_message_bytes(const WhirCommitment& commitment);

std::uint64_t serialized_message_bytes(
    const stir_whir_gr::algebra::GRContext& ctx, const WhirProof& proof);

std::uint64_t serialized_message_bytes(
    const stir_whir_gr::algebra::GRContext& ctx, const WhirOpening& opening);

bool proof_shape_valid(const WhirProof& proof);

void absorb_public_parameters(stir_whir_gr::crypto::Transcript& transcript,
                              const WhirPublicParameters& pp);

void absorb_opening_preamble(stir_whir_gr::crypto::Transcript& transcript,
                             const WhirCommitment& commitment,
                             std::span<const stir_whir_gr::algebra::GRElem> point,
                             const stir_whir_gr::algebra::GRElem& value);

void absorb_sumcheck_polynomial(stir_whir_gr::crypto::Transcript& transcript,
                                const stir_whir_gr::algebra::GRContext& ctx,
                                std::string_view label,
                                const WhirSumcheckPolynomial& polynomial);

std::vector<std::uint64_t> derive_unique_positions(
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count);

std::vector<std::vector<std::uint8_t>> build_oracle_leaves(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals);

stir_whir_gr::crypto::MerkleTree build_oracle_tree(
    stir_whir_gr::HashProfile profile, const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals);

}  // namespace stir_whir_gr::whir

#endif  // STIR_WHIR_GR_WHIR_COMMON_HPP_
