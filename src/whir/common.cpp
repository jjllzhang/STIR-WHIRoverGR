#include "whir/common.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace swgr::whir {
namespace {

template <typename Sink>
void SerializeRingElement(Sink& sink, const swgr::algebra::GRContext& ctx,
                          const swgr::algebra::GRElem& value) {
  swgr::SerializeBytes(sink, ctx.serialize(value));
}

template <typename Sink>
void SerializeMerkleProof(Sink& sink, const swgr::crypto::MerkleProof& proof) {
  swgr::SerializeUint64Vector(sink, proof.queried_indices);
  swgr::SerializeByteVector(sink, proof.leaf_payloads);
  swgr::SerializeByteVector(sink, proof.sibling_hashes);
}

template <typename Sink>
void SerializeSumcheckPolynomial(
    Sink& sink, const swgr::algebra::GRContext& ctx,
    const WhirSumcheckPolynomial& polynomial) {
  swgr::SerializeUint64(
      sink, static_cast<std::uint64_t>(polynomial.coefficients.size()));
  for (const auto& coefficient : polynomial.coefficients) {
    SerializeRingElement(sink, ctx, coefficient);
  }
}

template <typename Sink>
void SerializeWhirProofBody(Sink& sink, const swgr::algebra::GRContext& ctx,
                            const WhirProof& proof) {
  swgr::SerializeUint64(sink, static_cast<std::uint64_t>(proof.rounds.size()));
  for (const auto& round : proof.rounds) {
    swgr::SerializeUint64(
        sink,
        static_cast<std::uint64_t>(round.sumcheck_polynomials.size()));
    for (const auto& polynomial : round.sumcheck_polynomials) {
      SerializeSumcheckPolynomial(sink, ctx, polynomial);
    }
    swgr::SerializeBytes(sink, round.g_root);
    SerializeMerkleProof(sink, round.virtual_fold_openings);
  }
  SerializeRingElement(sink, ctx, proof.final_constant);
  SerializeMerkleProof(sink, proof.final_openings);
}

bool MerkleProofShapeValid(const swgr::crypto::MerkleProof& proof) {
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

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const WhirProof& proof) {
  swgr::CountingSink sink;
  SerializeWhirProofBody(sink, ctx, proof);
  return sink.size();
}

std::uint64_t serialized_message_bytes(
    const swgr::algebra::GRContext& ctx, const WhirOpening& opening) {
  swgr::CountingSink sink;
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

}  // namespace swgr::whir
