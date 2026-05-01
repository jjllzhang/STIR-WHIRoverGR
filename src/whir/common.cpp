#include "whir/common.hpp"

#include <cstdint>
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stir_whir_gr::whir {
namespace {

class ByteSink {
 public:
  void append_byte(std::uint8_t byte) { bytes_.push_back(byte); }

  void append_bytes(std::span<const std::uint8_t> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  const std::vector<std::uint8_t>& bytes() const { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
};

template <typename Sink>
void SerializeRingElement(Sink& sink, const stir_whir_gr::algebra::GRContext& ctx,
                          const stir_whir_gr::algebra::GRElem& value) {
  stir_whir_gr::SerializeBytes(sink, ctx.serialize(value));
}

template <typename Sink>
void SerializeMerkleProof(Sink& sink, const stir_whir_gr::crypto::MerkleProof& proof) {
  stir_whir_gr::SerializeUint64Vector(sink, proof.queried_indices);
  stir_whir_gr::SerializeByteVector(sink, proof.leaf_payloads);
  stir_whir_gr::SerializeByteVector(sink, proof.sibling_hashes);
}

template <typename Sink>
void SerializeSumcheckPolynomial(
    Sink& sink, const stir_whir_gr::algebra::GRContext& ctx,
    const WhirSumcheckPolynomial& polynomial) {
  stir_whir_gr::SerializeUint64(
      sink, static_cast<std::uint64_t>(polynomial.coefficients.size()));
  for (const auto& coefficient : polynomial.coefficients) {
    SerializeRingElement(sink, ctx, coefficient);
  }
}

template <typename Sink>
void SerializeWhirProofBody(Sink& sink, const stir_whir_gr::algebra::GRContext& ctx,
                            const WhirProof& proof) {
  stir_whir_gr::SerializeUint64(sink, static_cast<std::uint64_t>(proof.rounds.size()));
  for (const auto& round : proof.rounds) {
    stir_whir_gr::SerializeUint64(
        sink,
        static_cast<std::uint64_t>(round.sumcheck_polynomials.size()));
    for (const auto& polynomial : round.sumcheck_polynomials) {
      SerializeSumcheckPolynomial(sink, ctx, polynomial);
    }
    stir_whir_gr::SerializeBytes(sink, round.g_root);
    SerializeMerkleProof(sink, round.virtual_fold_openings);
  }
  SerializeRingElement(sink, ctx, proof.final_constant);
  SerializeMerkleProof(sink, proof.final_openings);
}

template <typename Sink>
void SerializePublicParameters(Sink& sink, const WhirPublicParameters& pp) {
  const auto& ctx = *pp.ctx;
  stir_whir_gr::SerializeUint64(sink, pp.variable_count);
  stir_whir_gr::SerializeUint64(sink, pp.initial_domain.size());
  SerializeRingElement(sink, ctx, pp.initial_domain.offset());
  SerializeRingElement(sink, ctx, pp.initial_domain.root());
  SerializeRingElement(sink, ctx, pp.omega);
  stir_whir_gr::SerializeUint64(sink, pp.lambda_target);
  stir_whir_gr::SerializeUint64(sink, static_cast<std::uint64_t>(pp.hash_profile));
  stir_whir_gr::SerializeUint64Vector(sink, pp.layer_widths);
  stir_whir_gr::SerializeUint64Vector(sink, pp.shift_repetitions);
  stir_whir_gr::SerializeUint64(sink, pp.final_repetitions);
  stir_whir_gr::SerializeUint64Vector(sink, pp.degree_bounds);
}

template <typename Sink>
void SerializeRingVector(Sink& sink, const stir_whir_gr::algebra::GRContext& ctx,
                         std::span<const stir_whir_gr::algebra::GRElem> values) {
  stir_whir_gr::SerializeUint64(sink, static_cast<std::uint64_t>(values.size()));
  for (const auto& value : values) {
    SerializeRingElement(sink, ctx, value);
  }
}

bool MerkleProofShapeValid(const stir_whir_gr::crypto::MerkleProof& proof) {
  return !proof.queried_indices.empty() &&
         proof.queried_indices.size() == proof.leaf_payloads.size();
}

}  // namespace

std::string indexed_label(std::string_view prefix, std::uint64_t round) {
  std::string label(prefix);
  label.push_back(':');
  label += std::to_string(round);
  return label;
}

std::string indexed_label(std::string_view prefix, std::uint64_t round,
                          std::uint64_t index) {
  std::string label = indexed_label(prefix, round);
  label.push_back(':');
  label += std::to_string(index);
  return label;
}

std::uint64_t serialized_message_bytes(const WhirCommitment& commitment) {
  stir_whir_gr::CountingSink sink;
  stir_whir_gr::SerializeBytes(sink, commitment.oracle_root);
  return sink.size();
}

std::uint64_t serialized_message_bytes(
    const stir_whir_gr::algebra::GRContext& ctx, const WhirProof& proof) {
  stir_whir_gr::CountingSink sink;
  SerializeWhirProofBody(sink, ctx, proof);
  return sink.size();
}

std::uint64_t serialized_message_bytes(
    const stir_whir_gr::algebra::GRContext& ctx, const WhirOpening& opening) {
  stir_whir_gr::CountingSink sink;
  SerializeRingElement(sink, ctx, opening.value);
  SerializeWhirProofBody(sink, ctx, opening.proof);
  return sink.size();
}

bool proof_shape_valid(const WhirProof& proof) {
  if (proof.rounds.empty() || !MerkleProofShapeValid(proof.final_openings)) {
    return false;
  }
  for (const auto& round : proof.rounds) {
    if (round.sumcheck_polynomials.empty() || round.g_root.empty() ||
        !MerkleProofShapeValid(round.virtual_fold_openings)) {
      return false;
    }
    for (const auto& polynomial : round.sumcheck_polynomials) {
      if (polynomial.coefficients.empty()) {
        return false;
      }
    }
  }
  return true;
}

void absorb_public_parameters(stir_whir_gr::crypto::Transcript& transcript,
                              const WhirPublicParameters& pp) {
  ByteSink sink;
  SerializePublicParameters(sink, pp);
  transcript.absorb_labeled_bytes(kTranscriptLabelPublicParameters,
                                  sink.bytes());
}

void absorb_opening_preamble(stir_whir_gr::crypto::Transcript& transcript,
                             const WhirCommitment& commitment,
                             std::span<const stir_whir_gr::algebra::GRElem> point,
                             const stir_whir_gr::algebra::GRElem& value) {
  const auto& ctx = *commitment.public_params.ctx;
  absorb_public_parameters(transcript, commitment.public_params);
  transcript.absorb_labeled_bytes(kTranscriptLabelCommitment,
                                  commitment.oracle_root);

  ByteSink point_sink;
  SerializeRingVector(point_sink, ctx, point);
  transcript.absorb_labeled_bytes(kTranscriptLabelOpenPoint,
                                  point_sink.bytes());

  ByteSink value_sink;
  SerializeRingElement(value_sink, ctx, value);
  transcript.absorb_labeled_bytes(kTranscriptLabelOpenValue,
                                  value_sink.bytes());
}

void absorb_sumcheck_polynomial(stir_whir_gr::crypto::Transcript& transcript,
                                const stir_whir_gr::algebra::GRContext& ctx,
                                std::string_view label,
                                const WhirSumcheckPolynomial& polynomial) {
  ByteSink sink;
  SerializeSumcheckPolynomial(sink, ctx, polynomial);
  transcript.absorb_labeled_bytes(label, sink.bytes());
}

std::vector<std::uint64_t> derive_unique_positions(
    stir_whir_gr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t query_count) {
  if (modulus == 0 || query_count == 0) {
    return {};
  }

  const std::uint64_t capped_count = std::min(query_count, modulus);
  std::vector<std::uint64_t> positions;
  positions.reserve(static_cast<std::size_t>(capped_count));
  for (std::uint64_t attempt = 0;
       positions.size() < static_cast<std::size_t>(capped_count); ++attempt) {
    const auto candidate = transcript.challenge_index(
        std::string(label_prefix) + ":" + std::to_string(attempt), modulus);
    if (std::find(positions.begin(), positions.end(), candidate) ==
        positions.end()) {
      positions.push_back(candidate);
    }
  }
  return positions;
}

std::vector<std::vector<std::uint8_t>> build_oracle_leaves(
    const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals) {
  return ctx.with_ntl_context([&] {
    std::vector<std::vector<std::uint8_t>> leaves;
    leaves.reserve(oracle_evals.size());
    for (const auto& value : oracle_evals) {
      leaves.push_back(ctx.serialize(value));
    }
    return leaves;
  });
}

stir_whir_gr::crypto::MerkleTree build_oracle_tree(
    stir_whir_gr::HashProfile profile, const stir_whir_gr::algebra::GRContext& ctx,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle_evals) {
  return stir_whir_gr::crypto::MerkleTree(profile,
                                  build_oracle_leaves(ctx, oracle_evals));
}

}  // namespace stir_whir_gr::whir
