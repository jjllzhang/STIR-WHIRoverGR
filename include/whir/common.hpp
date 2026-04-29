#ifndef SWGR_WHIR_COMMON_HPP_
#define SWGR_WHIR_COMMON_HPP_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "algebra/gr_context.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "ldt.hpp"

namespace swgr::whir {

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

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const WhirProof& proof);

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const WhirOpening& opening);

bool proof_shape_valid(const WhirProof& proof);

}  // namespace swgr::whir

#endif  // SWGR_WHIR_COMMON_HPP_
