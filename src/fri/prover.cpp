#include "fri/prover.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <utility>

#include "crypto/fs/transcript.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"

namespace swgr::fri {
namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

std::string RoundLabel(const char* prefix, std::size_t round_index) {
  return std::string(prefix) + ":" + std::to_string(round_index);
}

std::uint64_t MerkleOpeningPayloadBytes(
    const swgr::crypto::MerkleProof& proof) {
  std::uint64_t bytes = 0;
  for (const auto& payload : proof.leaf_payloads) {
    bytes += static_cast<std::uint64_t>(payload.size());
  }
  for (const auto& sibling : proof.sibling_hashes) {
    bytes += static_cast<std::uint64_t>(sibling.size());
  }
  return bytes;
}

std::uint64_t PolynomialPayloadBytes(const swgr::algebra::GRContext& ctx,
                                     const swgr::poly_utils::Polynomial& poly) {
  return static_cast<std::uint64_t>(poly.coefficients().size()) *
         static_cast<std::uint64_t>(ctx.elem_bytes());
}

std::uint64_t CompactFriProofBytes(const swgr::algebra::GRContext& ctx,
                                   const FriProof& proof) {
  std::uint64_t bytes = 0;
  const std::size_t query_rounds =
      proof.rounds.empty() ? 0 : proof.rounds.size() - 1U;
  for (std::size_t round_index = 0;
       round_index < query_rounds && round_index < proof.oracle_roots.size();
       ++round_index) {
    bytes += static_cast<std::uint64_t>(proof.oracle_roots[round_index].size());
    bytes += MerkleOpeningPayloadBytes(proof.rounds[round_index].oracle_proof);
  }
  bytes += PolynomialPayloadBytes(ctx, proof.final_polynomial);
  return bytes;
}

}  // namespace

FriProver::FriProver(FriParameters params) : params_(std::move(params)) {}

FriProof FriProver::prove(
    const FriInstance& instance,
    const swgr::poly_utils::Polynomial& polynomial) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument("fri::FriProver::prove received invalid instance");
  }
  if (polynomial.degree() > instance.claimed_degree) {
    throw std::invalid_argument(
        "fri::FriProver::prove polynomial exceeds claimed degree");
  }

  FriProof proof;
  swgr::crypto::Transcript transcript(params_.hash_profile);
  Domain current_domain = instance.domain;
  std::uint64_t current_degree = instance.claimed_degree;
  const auto prover_start = std::chrono::steady_clock::now();
  double encode_ms = 0.0;
  double merkle_ms = 0.0;
  double transcript_ms = 0.0;
  double fold_ms = 0.0;
  double interpolate_ms = 0.0;
  double query_open_ms = 0.0;
  double commit_ms = 0.0;
  double query_phase_ms = 0.0;
  auto current_oracle = [&] {
    const auto encode_start = std::chrono::steady_clock::now();
    auto oracle = swgr::poly_utils::rs_encode(current_domain, polynomial);
    encode_ms += ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());
    return oracle;
  }();

  const std::size_t fold_rounds =
      folding_round_count(instance, params_.fold_factor, params_.stop_degree);
  const auto query_rounds = resolve_query_rounds_metadata(params_, instance);

  for (std::size_t round_index = 0; round_index < fold_rounds; ++round_index) {
    FriRoundProof round;
    round.round_index = static_cast<std::uint64_t>(round_index);
    round.domain_size = current_domain.size();
    round.oracle_evals = current_oracle;

    const auto commit_start = std::chrono::steady_clock::now();
    const auto merkle_start = std::chrono::steady_clock::now();
    const auto oracle_tree =
        build_oracle_tree(params_.hash_profile, current_domain.context(),
                          round.oracle_evals, params_.fold_factor);
    merkle_ms += ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());
    const auto oracle_commitment = oracle_tree.root();
    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(oracle_commitment);
    round.folding_alpha = derive_round_challenge(
        transcript, current_domain.context(),
        RoundLabel("fri.fold_alpha", round_index));
    round.query_positions = derive_query_positions(
        transcript, RoundLabel("fri.query", round_index),
        query_rounds[round_index].bundle_count,
        query_rounds[round_index].effective_query_count);
    transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());
    commit_ms += ElapsedMilliseconds(commit_start, std::chrono::steady_clock::now());

    const auto query_start = std::chrono::steady_clock::now();
    round.oracle_proof = oracle_tree.open(round.query_positions);
    const double open_elapsed =
        ElapsedMilliseconds(query_start, std::chrono::steady_clock::now());
    query_open_ms += open_elapsed;
    query_phase_ms += open_elapsed;

    proof.oracle_roots.push_back(oracle_commitment);
    proof.rounds.push_back(round);

    const auto fold_start = std::chrono::steady_clock::now();
    current_oracle = swgr::poly_utils::fold_table_k(
        current_domain, current_oracle, params_.fold_factor,
        round.folding_alpha);
    fold_ms += ElapsedMilliseconds(fold_start, std::chrono::steady_clock::now());
    current_domain = current_domain.pow_map(params_.fold_factor);
    current_degree /= params_.fold_factor;
  }

  FriRoundProof final_round;
  final_round.round_index = static_cast<std::uint64_t>(fold_rounds);
  final_round.domain_size = current_domain.size();
  final_round.folding_alpha = current_domain.context().zero();
  final_round.oracle_evals = current_oracle;
  const auto final_merkle_start = std::chrono::steady_clock::now();
  proof.oracle_roots.push_back(
      build_oracle_tree(params_.hash_profile, current_domain.context(),
                        final_round.oracle_evals, 1)
          .root());
  merkle_ms +=
      ElapsedMilliseconds(final_merkle_start, std::chrono::steady_clock::now());
  const auto interpolate_start = std::chrono::steady_clock::now();
  proof.final_polynomial =
      swgr::poly_utils::rs_interpolate(current_domain, final_round.oracle_evals);
  interpolate_ms +=
      ElapsedMilliseconds(interpolate_start, std::chrono::steady_clock::now());
  proof.rounds.push_back(std::move(final_round));

  if (proof.final_polynomial.degree() > current_degree) {
    throw std::runtime_error(
        "fri::FriProver::prove terminal polynomial violates degree bound");
  }

  const std::uint64_t serialized_bytes =
      CompactFriProofBytes(instance.domain.context(), proof);

  proof.stats.prover_rounds = static_cast<std::uint64_t>(fold_rounds);
  proof.stats.verifier_hashes = 0;
  for (const auto& round : proof.rounds) {
    proof.stats.verifier_hashes +=
        static_cast<std::uint64_t>(round.oracle_proof.sibling_hashes.size());
  }
  proof.stats.serialized_bytes = serialized_bytes;
  proof.stats.commit_ms = commit_ms;
  proof.stats.prove_query_phase_ms = query_phase_ms;
  proof.stats.prover_encode_ms = encode_ms;
  proof.stats.prover_merkle_ms = merkle_ms;
  proof.stats.prover_transcript_ms = transcript_ms;
  proof.stats.prover_fold_ms = fold_ms;
  proof.stats.prover_interpolate_ms = interpolate_ms;
  proof.stats.prover_query_open_ms = query_open_ms;
  proof.stats.prover_total_ms =
      ElapsedMilliseconds(prover_start, std::chrono::steady_clock::now());
  return proof;
}

}  // namespace swgr::fri
