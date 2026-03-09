#include "stir/prover.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

#include "fri/common.hpp"
#include "poly_utils/degree_correction.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/quotient.hpp"

namespace swgr::stir {
namespace {

std::uint64_t SaturatingSubtract(std::uint64_t lhs, std::uint64_t rhs) {
  return lhs >= rhs ? lhs - rhs : 0;
}

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

std::string RoundLabel(const char* prefix, std::size_t round_index) {
  return std::string(prefix) + ":" + std::to_string(round_index);
}

}  // namespace

StirProver::StirProver(StirParameters params) : params_(std::move(params)) {}

StirProofWithWitness StirProver::prove(
    const StirInstance& instance,
    const swgr::poly_utils::Polynomial& polynomial) const {
  return prove_with_witness(instance, polynomial);
}

StirProofWithWitness StirProver::prove_with_witness(
    const StirInstance& instance,
    const swgr::poly_utils::Polynomial& polynomial) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument(
        "stir::StirProver::prove received invalid STIR instance");
  }
  if (polynomial.degree() > instance.claimed_degree) {
    throw std::invalid_argument(
        "stir::StirProver::prove polynomial exceeds claimed degree");
  }

  const auto& ctx = instance.domain.context();
  StirProofWithWitness artifact;
  auto& proof = artifact.proof;
  auto& witness = artifact.witness;
  Domain current_domain = instance.domain;
  std::uint64_t current_degree_bound = instance.claimed_degree;
  swgr::poly_utils::Polynomial current_polynomial = polynomial;
  swgr::crypto::Transcript transcript(params_.hash_profile);

  const std::size_t round_count = folding_round_count(instance, params_);
  const auto query_metadata = resolve_query_schedule_metadata(params_, instance);
  const auto prover_start = std::chrono::steady_clock::now();
  double encode_ms = 0.0;
  double merkle_ms = 0.0;
  double transcript_ms = 0.0;
  double fold_ms = 0.0;
  double interpolate_ms = 0.0;
  double query_open_ms = 0.0;
  double ood_ms = 0.0;
  double answer_ms = 0.0;
  double quotient_ms = 0.0;
  double degree_correction_ms = 0.0;
  double commit_ms = 0.0;
  double query_phase_ms = 0.0;
  std::vector<swgr::algebra::GRElem> cached_input_oracle;
  bool has_cached_input_oracle = false;

  for (std::size_t round_index = 0; round_index < round_count; ++round_index) {
    const auto effective_query_count =
        query_metadata[round_index].effective_query_count;
    StirRoundProof round;
    StirRoundWitness round_witness;
    round.round_index = static_cast<std::uint64_t>(round_index);
    round.input_domain_size = current_domain.size();
    round.input_degree_bound = current_degree_bound;
    round_witness.input_polynomial = current_polynomial;

    std::vector<swgr::algebra::GRElem> computed_input_oracle;
    const auto encode_start = std::chrono::steady_clock::now();
    const auto* input_oracle = &cached_input_oracle;
    if (!has_cached_input_oracle) {
      computed_input_oracle =
          swgr::poly_utils::rs_encode(current_domain, current_polynomial);
      input_oracle = &computed_input_oracle;
    }
    encode_ms += ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());
    const auto commit_start = std::chrono::steady_clock::now();
    const auto input_merkle_start = std::chrono::steady_clock::now();
    const auto input_tree =
        swgr::fri::build_oracle_tree(params_.hash_profile, ctx, *input_oracle,
                                     params_.virtual_fold_factor);
    merkle_ms +=
        ElapsedMilliseconds(input_merkle_start, std::chrono::steady_clock::now());
    const auto input_root = input_tree.root();
    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(input_root);
    round.folding_alpha = swgr::fri::derive_round_challenge(
        transcript, ctx, RoundLabel("stir.fold_alpha", round_index));
    transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());

    const Domain folded_domain =
        current_domain.pow_map(params_.virtual_fold_factor);
    const Domain shift_domain = current_domain.scale_offset(params_.shift_power);
    round.folded_domain_size = folded_domain.size();
    round.shift_domain_size = shift_domain.size();
    round.folded_degree_bound =
        folded_degree_bound(current_degree_bound, params_.virtual_fold_factor);

    const auto fold_start = std::chrono::steady_clock::now();
    const auto folded_table = swgr::poly_utils::fold_table_k(
        current_domain, *input_oracle, params_.virtual_fold_factor,
        round.folding_alpha);
    fold_ms += ElapsedMilliseconds(fold_start, std::chrono::steady_clock::now());
    const auto interpolate_start = std::chrono::steady_clock::now();
    round_witness.folded_polynomial =
        swgr::poly_utils::rs_interpolate(folded_domain, folded_table);
    interpolate_ms +=
        ElapsedMilliseconds(interpolate_start, std::chrono::steady_clock::now());
    const auto shift_encode_start = std::chrono::steady_clock::now();
    round_witness.shifted_oracle_evals =
        swgr::poly_utils::rs_encode(shift_domain, round_witness.folded_polynomial);
    encode_ms +=
        ElapsedMilliseconds(shift_encode_start, std::chrono::steady_clock::now());

    const auto shift_merkle_start = std::chrono::steady_clock::now();
    const auto shift_tree = swgr::fri::build_oracle_tree(
        params_.hash_profile, ctx, round_witness.shifted_oracle_evals, 1);
    merkle_ms +=
        ElapsedMilliseconds(shift_merkle_start, std::chrono::steady_clock::now());
    const auto oracle_root = shift_tree.root();
    proof.oracle_roots.push_back(oracle_root);
    const auto transcript_shift_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(oracle_root);
    transcript_ms += ElapsedMilliseconds(transcript_shift_start,
                                         std::chrono::steady_clock::now());
    commit_ms += ElapsedMilliseconds(commit_start, std::chrono::steady_clock::now());

    const auto query_start = std::chrono::steady_clock::now();
    const auto ood_start = std::chrono::steady_clock::now();
    round.ood_points = derive_ood_points(
        current_domain, shift_domain, folded_domain, transcript,
        RoundLabel("stir.ood", round_index), params_.ood_samples);
    round.ood_answers.reserve(round.ood_points.size());
    for (const auto& point : round.ood_points) {
      round.ood_answers.push_back(round_witness.folded_polynomial.evaluate(ctx, point));
    }
    ood_ms += ElapsedMilliseconds(ood_start, std::chrono::steady_clock::now());
    const auto transcript_query_start = std::chrono::steady_clock::now();
    for (const auto& answer : round.ood_answers) {
      transcript.absorb_ring(ctx, answer);
    }
    round.comb_randomness = swgr::fri::derive_round_challenge(
        transcript, ctx, RoundLabel("stir.comb", round_index));
    round.fold_query_positions = derive_unique_positions(
        transcript, RoundLabel("stir.fold_query", round_index),
        folded_domain.size(), effective_query_count);
    round.shift_query_positions = derive_unique_positions(
        transcript, RoundLabel("stir.shift_query", round_index),
        shift_domain.size(), effective_query_count);
    transcript_ms += ElapsedMilliseconds(transcript_query_start,
                                         std::chrono::steady_clock::now());

    const auto open_start = std::chrono::steady_clock::now();
    round.input_oracle_proof = input_tree.open(round.fold_query_positions);
    round.shift_oracle_proof = shift_tree.open(round.shift_query_positions);
    const double open_elapsed =
        ElapsedMilliseconds(open_start, std::chrono::steady_clock::now());
    query_open_ms += open_elapsed;
    round.shift_query_answers.reserve(round.shift_query_positions.size());

    std::vector<swgr::algebra::GRElem> answer_points = round.ood_points;
    std::vector<swgr::algebra::GRElem> answer_values = round.ood_answers;
    answer_points.reserve(answer_points.size() + round.shift_query_positions.size());
    answer_values.reserve(answer_values.size() +
                          round.shift_query_positions.size());
    for (const auto position : round.shift_query_positions) {
      round.shift_query_answers.push_back(
          round_witness
              .shifted_oracle_evals[static_cast<std::size_t>(position)]);
      answer_points.push_back(shift_domain.element(position));
      answer_values.push_back(round.shift_query_answers.back());
    }
    query_phase_ms +=
        ElapsedMilliseconds(query_start, std::chrono::steady_clock::now());

    const auto answer_start = std::chrono::steady_clock::now();
    round_witness.answer_polynomial =
        swgr::poly_utils::answer_polynomial(ctx, answer_points, answer_values);
    round_witness.vanishing_polynomial =
        swgr::poly_utils::vanishing_polynomial(ctx, answer_points);
    answer_ms += ElapsedMilliseconds(answer_start, std::chrono::steady_clock::now());

    const auto quotient_start = std::chrono::steady_clock::now();
    round_witness.quotient_polynomial =
        swgr::poly_utils::quotient_polynomial_from_answers(
            ctx, round_witness.folded_polynomial, answer_points, answer_values);
    quotient_ms +=
        ElapsedMilliseconds(quotient_start, std::chrono::steady_clock::now());

    const std::uint64_t quotient_degree_bound =
        SaturatingSubtract(round.folded_degree_bound, answer_points.size());
    const auto degree_correction_start = std::chrono::steady_clock::now();
    round_witness.next_polynomial = swgr::poly_utils::degree_correction_polynomial(
        ctx, round_witness.quotient_polynomial, round.folded_degree_bound,
        quotient_degree_bound, round.comb_randomness);
    degree_correction_ms += ElapsedMilliseconds(
        degree_correction_start, std::chrono::steady_clock::now());
    if (round_witness.next_polynomial.degree() > round.folded_degree_bound) {
      throw std::runtime_error(
          "stir::StirProver::prove degree correction exceeded target degree");
    }

    std::vector<swgr::algebra::GRElem> next_input_oracle;
    bool has_next_input_oracle = false;
    if (round_index + 1U < round_count) {
      const auto next_encode_start = std::chrono::steady_clock::now();
      has_next_input_oracle = try_reuse_next_round_input_oracle(
          shift_domain, round_witness.shifted_oracle_evals,
          round_witness.answer_polynomial,
          round_witness.vanishing_polynomial,
          round_witness.quotient_polynomial,
          round.comb_randomness, round.folded_degree_bound,
          quotient_degree_bound, &next_input_oracle);
      if (!has_next_input_oracle) {
        next_input_oracle =
            swgr::poly_utils::rs_encode(shift_domain, round_witness.next_polynomial);
        has_next_input_oracle = true;
      }
      encode_ms += ElapsedMilliseconds(next_encode_start,
                                       std::chrono::steady_clock::now());
    }

    proof.rounds.push_back(round);
    witness.rounds.push_back(std::move(round_witness));
    current_domain = shift_domain;
    current_degree_bound = proof.rounds.back().folded_degree_bound;
    current_polynomial = witness.rounds.back().next_polynomial;
    cached_input_oracle = std::move(next_input_oracle);
    has_cached_input_oracle = has_next_input_oracle;
  }

  proof.final_polynomial = current_polynomial;
  if (proof.final_polynomial.degree() > current_degree_bound) {
    throw std::runtime_error(
        "stir::StirProver::prove terminal polynomial violates degree bound");
  }
  proof.stats.prover_rounds = static_cast<std::uint64_t>(round_count);
  proof.stats.serialized_bytes = serialized_message_bytes(ctx, proof);
  proof.stats.verifier_hashes = 0;
  for (const auto& round : proof.rounds) {
    proof.stats.verifier_hashes +=
        static_cast<std::uint64_t>(round.input_oracle_proof.sibling_hashes.size() +
                                   round.shift_oracle_proof.sibling_hashes.size());
  }
  proof.stats.commit_ms = commit_ms;
  proof.stats.prove_query_phase_ms = query_phase_ms;
  proof.stats.prover_encode_ms = encode_ms;
  proof.stats.prover_merkle_ms = merkle_ms;
  proof.stats.prover_transcript_ms = transcript_ms;
  proof.stats.prover_fold_ms = fold_ms;
  proof.stats.prover_interpolate_ms = interpolate_ms;
  proof.stats.prover_query_open_ms = query_open_ms;
  proof.stats.prover_ood_ms = ood_ms;
  proof.stats.prover_answer_ms = answer_ms;
  proof.stats.prover_quotient_ms = quotient_ms;
  proof.stats.prover_degree_correction_ms = degree_correction_ms;
  proof.stats.prover_total_ms =
      ElapsedMilliseconds(prover_start, std::chrono::steady_clock::now());
  return artifact;
}

}  // namespace swgr::stir
