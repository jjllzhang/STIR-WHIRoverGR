#include "stir/verifier.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
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

std::string RoundLabel(const char* prefix, std::size_t round_index) {
  return std::string(prefix) + ":" + std::to_string(round_index);
}

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

bool SamePolynomial(const swgr::poly_utils::Polynomial& lhs,
                    const swgr::poly_utils::Polynomial& rhs) {
  return lhs.coefficients() == rhs.coefficients();
}

template <typename T>
bool SameVector(const std::vector<T>& lhs, const std::vector<T>& rhs) {
  return lhs == rhs;
}

std::vector<std::uint64_t> SortedPositions(
    const std::vector<std::uint64_t>& values) {
  auto sorted = values;
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

}  // namespace

StirVerifier::StirVerifier(StirParameters params) : params_(std::move(params)) {}

bool StirVerifier::verify(const StirInstance& instance,
                         const StirProof& proof,
                         swgr::ProofStatistics* stats) const {
  swgr::ProofStatistics local_stats;
  const auto verify_start = std::chrono::steady_clock::now();
  try {
    if (!validate(params_, instance)) {
      return false;
    }

    const std::size_t round_count = folding_round_count(instance, params_);
    if (proof.rounds.size() != round_count ||
        proof.oracle_roots.size() != round_count ||
        proof.stats.prover_rounds != round_count) {
      return false;
    }

    const auto schedule = resolve_query_repetitions(params_, instance);
    Domain current_domain = instance.domain;
    std::uint64_t current_degree_bound = instance.claimed_degree;
    swgr::poly_utils::Polynomial expected_current_polynomial =
        proof.rounds.empty() ? proof.final_polynomial
                             : proof.rounds.front().input_polynomial;
    swgr::crypto::Transcript transcript(params_.hash_profile);
    std::vector<swgr::algebra::GRElem> cached_input_oracle;
    bool has_cached_input_oracle = false;

    for (std::size_t round_index = 0; round_index < round_count; ++round_index) {
      const auto& round = proof.rounds[round_index];
      if (round.round_index != round_index ||
          round.input_domain_size != current_domain.size() ||
          round.input_degree_bound != current_degree_bound ||
          !SamePolynomial(round.input_polynomial, expected_current_polynomial) ||
          round.input_polynomial.degree() > current_degree_bound) {
        return false;
      }

      const auto& ctx = current_domain.context();
      const auto algebra_start = std::chrono::steady_clock::now();
      std::vector<swgr::algebra::GRElem> computed_input_oracle;
      const auto* input_oracle = &cached_input_oracle;
      if (!has_cached_input_oracle) {
        computed_input_oracle =
            swgr::poly_utils::rs_encode(current_domain, round.input_polynomial);
        input_oracle = &computed_input_oracle;
      }
      local_stats.verifier_algebra_ms +=
          ElapsedMilliseconds(algebra_start, std::chrono::steady_clock::now());
      const auto merkle_start = std::chrono::steady_clock::now();
      const auto input_tree = swgr::fri::build_oracle_tree(
          params_.hash_profile, ctx, *input_oracle, params_.virtual_fold_factor);
      const auto input_root = input_tree.root();
      local_stats.verifier_merkle_ms +=
          ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());
      const auto transcript_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(input_root);
      const auto expected_alpha = swgr::fri::derive_round_challenge(
          transcript, ctx, RoundLabel("stir.fold_alpha", round_index));
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_start, std::chrono::steady_clock::now());
      if (round.folding_alpha != expected_alpha) {
        return false;
      }

      const Domain folded_domain =
          current_domain.pow_map(params_.virtual_fold_factor);
      const Domain shift_domain = current_domain.scale_offset(params_.shift_power);
      const auto next_degree_bound = folded_degree_bound(
          current_degree_bound, params_.virtual_fold_factor);
      if (round.folded_domain_size != folded_domain.size() ||
          round.shift_domain_size != shift_domain.size() ||
          round.folded_degree_bound != next_degree_bound) {
        return false;
      }

      const auto algebra_round_start = std::chrono::steady_clock::now();
      const auto folded_table = swgr::poly_utils::fold_table_k(
          current_domain, *input_oracle, params_.virtual_fold_factor,
          round.folding_alpha);
      const auto expected_folded_polynomial =
          swgr::poly_utils::rs_interpolate(folded_domain, folded_table);
      if (!SamePolynomial(round.folded_polynomial, expected_folded_polynomial)) {
        return false;
      }

      const auto expected_shifted_oracle =
          swgr::poly_utils::rs_encode(shift_domain, expected_folded_polynomial);
      if (!SameVector(round.shifted_oracle_evals, expected_shifted_oracle)) {
        return false;
      }
      const auto shift_merkle_start = std::chrono::steady_clock::now();
      const auto shift_tree = swgr::fri::build_oracle_tree(
          params_.hash_profile, ctx, expected_shifted_oracle, 1);
      const auto oracle_root = shift_tree.root();
      local_stats.verifier_merkle_ms +=
          ElapsedMilliseconds(shift_merkle_start, std::chrono::steady_clock::now());
      if (oracle_root != proof.oracle_roots[round_index]) {
        return false;
      }
      const auto transcript_shift_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(oracle_root);
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_shift_start, std::chrono::steady_clock::now());

      const auto ood_start = std::chrono::steady_clock::now();
      const auto expected_ood_points = derive_ood_points(
          current_domain, shift_domain, folded_domain, transcript,
          RoundLabel("stir.ood", round_index), params_.ood_samples);
      if (!SameVector(round.ood_points, expected_ood_points)) {
        return false;
      }
      std::vector<swgr::algebra::GRElem> expected_ood_answers;
      expected_ood_answers.reserve(expected_ood_points.size());
      for (const auto& point : expected_ood_points) {
        expected_ood_answers.push_back(
            expected_folded_polynomial.evaluate(ctx, point));
      }
      local_stats.verifier_algebra_ms +=
          ElapsedMilliseconds(ood_start, std::chrono::steady_clock::now());
      const auto transcript_query_start = std::chrono::steady_clock::now();
      for (const auto& answer : expected_ood_answers) {
        transcript.absorb_ring(ctx, answer);
      }
      if (!SameVector(round.ood_answers, expected_ood_answers)) {
        return false;
      }

      const auto expected_combination = swgr::fri::derive_round_challenge(
          transcript, ctx, RoundLabel("stir.comb", round_index));
      if (round.comb_randomness != expected_combination) {
        return false;
      }

      const auto expected_fold_positions = derive_unique_positions(
          transcript, RoundLabel("stir.fold_query", round_index),
          folded_domain.size(), schedule[round_index]);
      const auto expected_shift_positions = derive_unique_positions(
          transcript, RoundLabel("stir.shift_query", round_index),
          shift_domain.size(), schedule[round_index]);
      const auto sorted_fold_positions = SortedPositions(expected_fold_positions);
      const auto sorted_shift_positions =
          SortedPositions(expected_shift_positions);
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_query_start, std::chrono::steady_clock::now());
      if (round.fold_query_positions != expected_fold_positions ||
          round.shift_query_positions != expected_shift_positions) {
        return false;
      }
      const auto verify_merkle_start = std::chrono::steady_clock::now();
      if (round.input_oracle_proof.queried_indices != sorted_fold_positions ||
          round.shift_oracle_proof.queried_indices != sorted_shift_positions ||
          !swgr::crypto::MerkleTree::verify(
              params_.hash_profile, folded_domain.size(), input_root,
              round.input_oracle_proof) ||
          !swgr::crypto::MerkleTree::verify(
              params_.hash_profile, shift_domain.size(), oracle_root,
              round.shift_oracle_proof)) {
        return false;
      }
      local_stats.verifier_merkle_ms += ElapsedMilliseconds(
          verify_merkle_start, std::chrono::steady_clock::now());

      std::vector<swgr::algebra::GRElem> expected_shift_answers;
      expected_shift_answers.reserve(expected_shift_positions.size());
      const auto query_phase_start = std::chrono::steady_clock::now();
      for (std::size_t i = 0; i < sorted_fold_positions.size(); ++i) {
        const auto position = sorted_fold_positions[i];
        if (round.input_oracle_proof.leaf_payloads[i] !=
            swgr::fri::serialize_oracle_bundle(
                ctx, *input_oracle, params_.virtual_fold_factor, position)) {
          return false;
        }
        const auto fiber_values = swgr::fri::deserialize_oracle_bundle(
            ctx, round.input_oracle_proof.leaf_payloads[i]);
        if (fiber_values.size() !=
            static_cast<std::size_t>(params_.virtual_fold_factor)) {
          return false;
        }
        std::vector<swgr::algebra::GRElem> fiber_points;
        fiber_points.reserve(static_cast<std::size_t>(params_.virtual_fold_factor));
        for (std::uint64_t fiber_offset = 0;
             fiber_offset < params_.virtual_fold_factor; ++fiber_offset) {
          fiber_points.push_back(
              current_domain.element(position + fiber_offset * folded_domain.size()));
        }
        const auto folded_value = ctx.with_ntl_context([&] {
          return swgr::poly_utils::fold_eval_k(
              fiber_points, fiber_values, expected_alpha);
        });
        if (folded_value !=
            folded_table[static_cast<std::size_t>(position)]) {
          return false;
        }
      }

      std::vector<swgr::algebra::GRElem> answer_points = expected_ood_points;
      std::vector<swgr::algebra::GRElem> answer_values = expected_ood_answers;
      for (std::size_t i = 0; i < sorted_shift_positions.size(); ++i) {
        const auto position = sorted_shift_positions[i];
        if (round.shift_oracle_proof.leaf_payloads[i] !=
                swgr::fri::serialize_oracle_bundle(ctx, expected_shifted_oracle, 1,
                                                   position)) {
          return false;
        }
      }
      for (const auto position : expected_shift_positions) {
        expected_shift_answers.push_back(
            expected_shifted_oracle[static_cast<std::size_t>(position)]);
        answer_points.push_back(shift_domain.element(position));
        answer_values.push_back(expected_shift_answers.back());
      }
      local_stats.verifier_query_phase_ms += ElapsedMilliseconds(
          query_phase_start, std::chrono::steady_clock::now());
      if (!SameVector(round.shift_query_answers, expected_shift_answers)) {
        return false;
      }

      const auto algebra_tail_start = std::chrono::steady_clock::now();
      const auto expected_answer_polynomial =
          swgr::poly_utils::answer_polynomial(ctx, answer_points, answer_values);
      const auto expected_vanishing_polynomial =
          swgr::poly_utils::vanishing_polynomial(ctx, answer_points);
      const auto expected_quotient_polynomial =
          swgr::poly_utils::quotient_polynomial_from_answers(
              ctx, expected_folded_polynomial, answer_points, answer_values);
      if (!SamePolynomial(round.answer_polynomial, expected_answer_polynomial) ||
          !SamePolynomial(round.vanishing_polynomial,
                          expected_vanishing_polynomial) ||
          !SamePolynomial(round.quotient_polynomial,
                          expected_quotient_polynomial)) {
        return false;
      }

      const auto quotient_degree_bound =
          SaturatingSubtract(next_degree_bound, answer_points.size());
      const auto expected_next_polynomial =
          swgr::poly_utils::degree_correction_polynomial(
              ctx, expected_quotient_polynomial, next_degree_bound,
              quotient_degree_bound, expected_combination);
      if (!SamePolynomial(round.next_polynomial, expected_next_polynomial) ||
          round.next_polynomial.degree() > next_degree_bound) {
        return false;
      }
      std::vector<swgr::algebra::GRElem> next_input_oracle;
      bool has_next_input_oracle = false;
      if (round_index + 1U < round_count) {
        has_next_input_oracle = try_reuse_next_round_input_oracle(
            shift_domain, expected_shifted_oracle, expected_answer_polynomial,
            expected_vanishing_polynomial, expected_quotient_polynomial,
            expected_combination, next_degree_bound, quotient_degree_bound,
            &next_input_oracle);
        if (!has_next_input_oracle) {
          next_input_oracle =
              swgr::poly_utils::rs_encode(shift_domain, expected_next_polynomial);
          has_next_input_oracle = true;
        }
      }
      local_stats.verifier_algebra_ms +=
          ElapsedMilliseconds(algebra_round_start, query_phase_start) +
          ElapsedMilliseconds(algebra_tail_start, std::chrono::steady_clock::now());

      expected_current_polynomial = expected_next_polynomial;
      current_domain = shift_domain;
      current_degree_bound = next_degree_bound;
      cached_input_oracle = std::move(next_input_oracle);
      has_cached_input_oracle = has_next_input_oracle;
    }

    const bool ok =
        SamePolynomial(proof.final_polynomial, expected_current_polynomial) &&
        proof.final_polynomial.degree() <= current_degree_bound;
    local_stats.verifier_total_ms =
        ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
    if (stats != nullptr) {
      *stats = local_stats;
    }
    return ok;
  } catch (...) {
    return false;
  }
}

}  // namespace swgr::stir
