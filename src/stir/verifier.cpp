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
#include "poly_utils/quotient.hpp"
#include "soundness/configurator.hpp"

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

swgr::algebra::GRElem DeriveFoldingChallenge(
    const StirParameters& params, swgr::crypto::Transcript& transcript,
    const swgr::algebra::GRContext& ctx, std::string_view label) {
  if (params.protocol_mode == StirProtocolMode::TheoremGr) {
    return derive_stir_folding_challenge(transcript, ctx, label);
  }
  return swgr::fri::derive_round_challenge(transcript, ctx, label);
}

swgr::algebra::GRElem DeriveCombChallenge(
    const StirParameters& params, swgr::crypto::Transcript& transcript,
    const swgr::algebra::GRContext& ctx, std::string_view label) {
  if (params.protocol_mode == StirProtocolMode::TheoremGr) {
    return derive_stir_comb_challenge(transcript, ctx, label);
  }
  return swgr::fri::derive_round_challenge(transcript, ctx, label);
}

std::vector<std::uint64_t> SortedPositions(
    const std::vector<std::uint64_t>& values) {
  auto sorted = values;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
  return sorted;
}

std::uint64_t ResolveFinalQueryCount(const StirParameters& params,
                                     std::uint64_t final_domain_size,
                                     std::uint64_t final_degree_bound,
                                     std::size_t round_count) {
  if (!params.query_repetitions.empty()) {
    const auto schedule =
        swgr::fri::query_schedule(round_count + 1U, params.query_repetitions);
    return std::min(schedule.back(), final_domain_size);
  }

  const double rho = static_cast<double>(final_degree_bound + 1U) /
                     static_cast<double>(final_domain_size);
  return std::min(
      swgr::soundness::auto_query_count_for_round(
          params.sec_mode, params.lambda_target, params.pow_bits, rho,
          round_count),
      final_domain_size);
}

struct VirtualFunctionState {
  swgr::algebra::GRElem comb_randomness;
  swgr::poly_utils::Polynomial ans_polynomial;
  std::vector<swgr::algebra::GRElem> quotient_set;
};

struct VerificationState {
  Domain domain;
  std::uint64_t degree_bound = 0;
  swgr::algebra::GRElem folding_alpha;
  bool initial = true;
  VirtualFunctionState virtual_function;
};

bool QueryCurrentFunction(const VerificationState& state,
                          const swgr::algebra::GRElem& evaluation_point,
                          const swgr::algebra::GRElem& actual_oracle_value,
                          swgr::algebra::GRElem* out) {
  if (out == nullptr) {
    return false;
  }
  if (state.initial) {
    *out = actual_oracle_value;
    return true;
  }

  const auto& ctx = state.domain.context();
  try {
    return ctx.with_ntl_context([&] {
      auto denominator = ctx.one();
      for (const auto& point : state.virtual_function.quotient_set) {
        const auto difference = evaluation_point - point;
        if (!ctx.is_unit(difference)) {
          return false;
        }
        denominator *= difference;
      }
      if (!ctx.is_unit(denominator)) {
        return false;
      }

      const auto ans_eval =
          state.virtual_function.ans_polynomial.evaluate(ctx, evaluation_point);
      const auto quotient_value = swgr::poly_utils::quotient_eval_with_hint(
          ctx, actual_oracle_value, evaluation_point,
          state.virtual_function.quotient_set, ctx.inv(denominator), ans_eval);
      *out = swgr::poly_utils::degree_correction_eval(
          ctx, evaluation_point, quotient_value, state.degree_bound,
          SaturatingSubtract(
              state.degree_bound,
              static_cast<std::uint64_t>(
                  state.virtual_function.quotient_set.size())),
          state.virtual_function.comb_randomness);
      return true;
    });
  } catch (...) {
    return false;
  }
}

bool DecodeBundle(const swgr::algebra::GRContext& ctx,
                  const std::vector<std::uint8_t>& payload,
                  std::uint64_t expected_bundle_size,
                  std::vector<swgr::algebra::GRElem>* bundle) {
  if (bundle == nullptr) {
    return false;
  }
  try {
    *bundle = swgr::fri::deserialize_oracle_bundle(ctx, payload);
  } catch (...) {
    return false;
  }
  return bundle->size() == static_cast<std::size_t>(expected_bundle_size);
}

bool ComputeFoldedAnswers(const VerificationState& state,
                          std::uint64_t fold_factor,
                          const std::vector<std::uint64_t>& query_positions,
                          const swgr::crypto::MerkleProof& proof,
                          std::vector<swgr::algebra::GRElem>* folded_answers) {
  if (folded_answers == nullptr ||
      proof.queried_indices != query_positions ||
      proof.leaf_payloads.size() != query_positions.size()) {
    return false;
  }

  const auto& ctx = state.domain.context();
  const std::uint64_t folded_domain_size = state.domain.size() / fold_factor;
  folded_answers->clear();
  folded_answers->reserve(query_positions.size());

  for (std::size_t query_index = 0; query_index < query_positions.size();
       ++query_index) {
    std::vector<swgr::algebra::GRElem> bundle_values;
    if (!DecodeBundle(ctx, proof.leaf_payloads[query_index], fold_factor,
                      &bundle_values)) {
      return false;
    }

    std::vector<swgr::algebra::GRElem> fiber_points;
    std::vector<swgr::algebra::GRElem> current_values;
    fiber_points.reserve(static_cast<std::size_t>(fold_factor));
    current_values.reserve(static_cast<std::size_t>(fold_factor));
    for (std::uint64_t fiber_offset = 0; fiber_offset < fold_factor;
         ++fiber_offset) {
      const auto point = state.domain.element(query_positions[query_index] +
                                              fiber_offset * folded_domain_size);
      swgr::algebra::GRElem current_value;
      if (!QueryCurrentFunction(state, point,
                                bundle_values[static_cast<std::size_t>(
                                    fiber_offset)],
                                &current_value)) {
        return false;
      }
      fiber_points.push_back(point);
      current_values.push_back(current_value);
    }

    try {
      folded_answers->push_back(ctx.with_ntl_context([&] {
        return swgr::poly_utils::fold_eval_k(fiber_points, current_values,
                                             state.folding_alpha);
      }));
    } catch (...) {
      return false;
    }
  }
  return true;
}

bool CheckShakeConsistency(
    const swgr::algebra::GRContext& ctx,
    const swgr::poly_utils::Polynomial& ans_polynomial,
    const swgr::poly_utils::Polynomial& shake_polynomial,
    const std::vector<swgr::algebra::GRElem>& quotient_points,
    const std::vector<swgr::algebra::GRElem>& quotient_values,
    const swgr::algebra::GRElem& shake_point) {
  if (quotient_points.size() != quotient_values.size()) {
    return false;
  }

  try {
    return ctx.with_ntl_context([&] {
      std::vector<swgr::algebra::GRElem> denominators;
      denominators.reserve(quotient_points.size());
      for (const auto& point : quotient_points) {
        const auto difference = shake_point - point;
        if (!ctx.is_unit(difference)) {
          return false;
        }
        denominators.push_back(difference);
      }
      const auto denominator_inverses = ctx.batch_inv(denominators);
      const auto ans_eval = ans_polynomial.evaluate(ctx, shake_point);
      const auto shake_eval = shake_polynomial.evaluate(ctx, shake_point);

      auto expected = ctx.zero();
      for (std::size_t i = 0; i < quotient_values.size(); ++i) {
        expected +=
            (ans_eval - quotient_values[i]) * denominator_inverses[i];
      }
      return static_cast<bool>(shake_eval == expected);
    });
  } catch (...) {
    return false;
  }
}

}  // namespace

StirVerifier::StirVerifier(StirParameters params) : params_(std::move(params)) {}

bool StirVerifier::verify(const StirInstance& instance, const StirProof& proof,
                          swgr::ProofStatistics* stats) const {
  swgr::ProofStatistics local_stats;
  const auto verify_start = std::chrono::steady_clock::now();
  try {
    if (!validate(params_, instance)) {
      return false;
    }

    const std::size_t round_count = folding_round_count(instance, params_);
    if (proof.rounds.size() != round_count ||
        proof.stats.prover_rounds != round_count) {
      return false;
    }

    const auto query_metadata = resolve_query_schedule_metadata(params_, instance);
    if (query_metadata.size() != round_count) {
      return false;
    }

    const auto& ctx = instance.domain.context();
    swgr::crypto::Transcript transcript(params_.hash_profile);
    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(proof.initial_root);
    VerificationState state{
        .domain = instance.domain,
        .degree_bound = instance.claimed_degree,
        .folding_alpha = DeriveFoldingChallenge(
            params_, transcript, ctx, RoundLabel("stir.fold_alpha", 0)),
        .initial = true,
        .virtual_function = {},
    };
    local_stats.verifier_transcript_ms += ElapsedMilliseconds(
        transcript_start, std::chrono::steady_clock::now());

    std::vector<std::uint8_t> current_root = proof.initial_root;

    for (std::size_t round_index = 0; round_index < round_count; ++round_index) {
      const auto& round = proof.rounds[round_index];
      const auto effective_query_count =
          query_metadata[round_index].effective_query_count;
      const Domain folded_domain =
          state.domain.pow_map(params_.virtual_fold_factor);
      const Domain shift_domain = state.domain.scale_offset(params_.shift_power);
      const std::uint64_t next_degree_bound =
          folded_degree_bound(state.degree_bound, params_.virtual_fold_factor);

      const auto merkle_start = std::chrono::steady_clock::now();
      if (!swgr::crypto::MerkleTree::verify(
              params_.hash_profile, folded_domain.size(), current_root,
              round.queries_to_prev)) {
        return false;
      }
      local_stats.verifier_merkle_ms +=
          ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());

      const auto round_transcript_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(round.g_root);
      const auto ood_points = derive_ood_points(
          params_, state.domain, shift_domain, folded_domain, transcript,
          RoundLabel("stir.ood", round_index), params_.ood_samples);
      if (round.betas.size() != ood_points.size()) {
        return false;
      }
      for (const auto& beta : round.betas) {
        transcript.absorb_ring(ctx, beta);
      }
      const auto comb_randomness = DeriveCombChallenge(
          params_, transcript, ctx, RoundLabel("stir.comb", round_index));
      const auto next_folding_alpha = DeriveFoldingChallenge(
          params_, transcript, ctx, RoundLabel("stir.fold_alpha", round_index + 1U));
      const auto query_positions = SortedPositions(derive_unique_positions(
          transcript, RoundLabel("stir.query", round_index), folded_domain.size(),
          effective_query_count));
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          round_transcript_start, std::chrono::steady_clock::now());

      const auto query_phase_start = std::chrono::steady_clock::now();
      std::vector<swgr::algebra::GRElem> folded_answers;
      if (!ComputeFoldedAnswers(state, params_.virtual_fold_factor, query_positions,
                                round.queries_to_prev, &folded_answers)) {
        return false;
      }
      local_stats.verifier_query_phase_ms += ElapsedMilliseconds(
          query_phase_start, std::chrono::steady_clock::now());

      std::vector<swgr::algebra::GRElem> quotient_points = ood_points;
      std::vector<swgr::algebra::GRElem> quotient_values = round.betas;
      quotient_points.reserve(quotient_points.size() + query_positions.size());
      quotient_values.reserve(quotient_values.size() + query_positions.size());
      for (std::size_t i = 0; i < query_positions.size(); ++i) {
        quotient_points.push_back(folded_domain.element(query_positions[i]));
        quotient_values.push_back(folded_answers[i]);
      }

      const auto shake_transcript_start = std::chrono::steady_clock::now();
      const auto shake_point = derive_shake_point(
          params_, state.domain, shift_domain, folded_domain, quotient_points,
          transcript, RoundLabel("stir.shake", round_index));
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          shake_transcript_start, std::chrono::steady_clock::now());

      const auto algebra_start = std::chrono::steady_clock::now();
      if (!CheckShakeConsistency(ctx, round.ans_polynomial, round.shake_polynomial,
                                 quotient_points, quotient_values, shake_point)) {
        return false;
      }
      local_stats.verifier_algebra_ms +=
          ElapsedMilliseconds(algebra_start, std::chrono::steady_clock::now());

      current_root = round.g_root;
      state = VerificationState{
          .domain = shift_domain,
          .degree_bound = next_degree_bound,
          .folding_alpha = next_folding_alpha,
          .initial = false,
          .virtual_function =
              VirtualFunctionState{
                  .comb_randomness = comb_randomness,
                  .ans_polynomial = round.ans_polynomial,
                  .quotient_set = quotient_points,
              },
      };
    }

    const Domain final_domain =
        state.domain.pow_map(params_.virtual_fold_factor);
    const std::uint64_t final_degree_bound =
        folded_degree_bound(state.degree_bound, params_.virtual_fold_factor);
    if (proof.final_polynomial.degree() > final_degree_bound) {
      return false;
    }

    const auto final_transcript_start = std::chrono::steady_clock::now();
    const auto final_query_count = ResolveFinalQueryCount(
        params_, final_domain.size(), final_degree_bound, round_count);
    const auto final_queries = SortedPositions(derive_unique_positions(
        transcript, RoundLabel("stir.final_query", round_count),
        final_domain.size(), final_query_count));
    local_stats.verifier_transcript_ms += ElapsedMilliseconds(
        final_transcript_start, std::chrono::steady_clock::now());

    const auto final_merkle_start = std::chrono::steady_clock::now();
    if (!swgr::crypto::MerkleTree::verify(
            params_.hash_profile, final_domain.size(), current_root,
            proof.queries_to_final)) {
      return false;
    }
    local_stats.verifier_merkle_ms += ElapsedMilliseconds(
        final_merkle_start, std::chrono::steady_clock::now());

    const auto final_query_phase_start = std::chrono::steady_clock::now();
    std::vector<swgr::algebra::GRElem> final_folded_answers;
    if (!ComputeFoldedAnswers(state, params_.virtual_fold_factor, final_queries,
                              proof.queries_to_final, &final_folded_answers)) {
      return false;
    }
    local_stats.verifier_query_phase_ms += ElapsedMilliseconds(
        final_query_phase_start, std::chrono::steady_clock::now());

    const auto final_algebra_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < final_queries.size(); ++i) {
      const auto expected =
          proof.final_polynomial.evaluate(ctx, final_domain.element(final_queries[i]));
      if (expected != final_folded_answers[i]) {
        return false;
      }
    }
    local_stats.verifier_algebra_ms += ElapsedMilliseconds(
        final_algebra_start, std::chrono::steady_clock::now());

    local_stats.verifier_total_ms =
        ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
    if (stats != nullptr) {
      *stats = local_stats;
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool StirVerifier::verify(const StirInstance& instance,
                          const StirProofWithWitness& artifact,
                          swgr::ProofStatistics* stats) const {
  return verify(instance, artifact.proof, stats);
}

}  // namespace swgr::stir
