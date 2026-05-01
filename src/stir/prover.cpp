#include "stir/prover.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fri/common.hpp"
#include "poly_utils/degree_correction.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/quotient.hpp"
#include "soundness/configurator.hpp"

namespace stir_whir_gr::stir {
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

stir_whir_gr::algebra::GRElem DeriveFoldingChallenge(
    const StirParameters& params, stir_whir_gr::crypto::Transcript& transcript,
    const stir_whir_gr::algebra::GRContext& ctx, std::string_view label) {
  if (params.protocol_mode == StirProtocolMode::TheoremGr) {
    return derive_stir_folding_challenge(transcript, ctx, label);
  }
  return stir_whir_gr::fri::derive_round_challenge(transcript, ctx, label);
}

stir_whir_gr::algebra::GRElem DeriveCombChallenge(
    const StirParameters& params, stir_whir_gr::crypto::Transcript& transcript,
    const stir_whir_gr::algebra::GRContext& ctx, std::string_view label) {
  if (params.protocol_mode == StirProtocolMode::TheoremGr) {
    return derive_stir_comb_challenge(transcript, ctx, label);
  }
  return stir_whir_gr::fri::derive_round_challenge(transcript, ctx, label);
}

std::vector<std::uint64_t> SortedPositions(
    const std::vector<std::uint64_t>& positions) {
  auto sorted = positions;
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
        stir_whir_gr::fri::query_schedule(round_count + 1U, params.query_repetitions);
    return std::min(schedule.back(), final_domain_size);
  }

  const double rho = static_cast<double>(final_degree_bound + 1U) /
                     static_cast<double>(final_domain_size);
  return std::min(
      stir_whir_gr::soundness::auto_query_count_for_round(
          params.sec_mode, params.lambda_target, params.pow_bits, rho,
          round_count),
      final_domain_size);
}

stir_whir_gr::poly_utils::Polynomial AddPolynomials(
    const stir_whir_gr::algebra::GRContext& ctx,
    const stir_whir_gr::poly_utils::Polynomial& lhs,
    const stir_whir_gr::poly_utils::Polynomial& rhs) {
  return ctx.with_ntl_context([&] {
    const auto& lhs_coeffs = lhs.coefficients();
    const auto& rhs_coeffs = rhs.coefficients();
    const std::size_t out_size =
        std::max(lhs_coeffs.size(), rhs_coeffs.size());
    std::vector<stir_whir_gr::algebra::GRElem> coefficients(out_size, ctx.zero());
    for (std::size_t i = 0; i < lhs_coeffs.size(); ++i) {
      coefficients[i] += lhs_coeffs[i];
    }
    for (std::size_t i = 0; i < rhs_coeffs.size(); ++i) {
      coefficients[i] += rhs_coeffs[i];
    }
    return stir_whir_gr::poly_utils::Polynomial(std::move(coefficients));
  });
}

stir_whir_gr::poly_utils::Polynomial SubtractConstant(
    const stir_whir_gr::algebra::GRContext& ctx,
    const stir_whir_gr::poly_utils::Polynomial& polynomial,
    const stir_whir_gr::algebra::GRElem& constant_term) {
  return ctx.with_ntl_context([&] {
    auto coefficients = polynomial.coefficients();
    if (coefficients.empty()) {
      coefficients.push_back(ctx.zero());
    }
    coefficients.front() -= constant_term;
    return stir_whir_gr::poly_utils::Polynomial(std::move(coefficients));
  });
}

stir_whir_gr::poly_utils::Polynomial DivideByLinearFactor(
    const stir_whir_gr::algebra::GRContext& ctx,
    const stir_whir_gr::poly_utils::Polynomial& numerator,
    const stir_whir_gr::algebra::GRElem& root) {
  return ctx.with_ntl_context([&] {
    std::vector<stir_whir_gr::algebra::GRElem> denominator(2U, ctx.zero());
    denominator.front() -= root;
    denominator.back() = ctx.one();
    return stir_whir_gr::poly_utils::quotient_polynomial(
        ctx, numerator, stir_whir_gr::poly_utils::Polynomial(std::move(denominator)));
  });
}

stir_whir_gr::poly_utils::Polynomial BuildShakePolynomial(
    const stir_whir_gr::algebra::GRContext& ctx,
    const stir_whir_gr::poly_utils::Polynomial& ans_polynomial,
    const std::vector<stir_whir_gr::algebra::GRElem>& points,
    const std::vector<stir_whir_gr::algebra::GRElem>& values) {
  if (points.size() != values.size()) {
    throw std::invalid_argument(
        "BuildShakePolynomial requires equal-sized point/value inputs");
  }

  stir_whir_gr::poly_utils::Polynomial accumulator;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto numerator = SubtractConstant(ctx, ans_polynomial, values[i]);
    const auto term = DivideByLinearFactor(ctx, numerator, points[i]);
    accumulator = AddPolynomials(ctx, accumulator, term);
  }
  return accumulator;
}

}  // namespace

StirProver::StirProver(StirParameters params) : params_(std::move(params)) {}

StirProof StirProver::prove(const StirInstance& instance,
                            const stir_whir_gr::poly_utils::Polynomial& polynomial) const {
  return prove_with_witness(instance, polynomial).proof;
}

StirProofWithWitness StirProver::prove_with_witness(
    const StirInstance& instance,
    const stir_whir_gr::poly_utils::Polynomial& polynomial) const {
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
  const std::size_t round_count = folding_round_count(instance, params_);
  const auto query_metadata = resolve_query_schedule_metadata(params_, instance);
  if (query_metadata.size() != round_count) {
    throw std::runtime_error(
        "stir::StirProver::prove query schedule metadata mismatch");
  }

  Domain current_domain = instance.domain;
  std::uint64_t current_degree_bound = instance.claimed_degree;
  stir_whir_gr::poly_utils::Polynomial current_polynomial = polynomial;
  stir_whir_gr::crypto::Transcript transcript(params_.hash_profile);

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

  const auto initial_encode_start = std::chrono::steady_clock::now();
  auto current_actual_oracle =
      stir_whir_gr::poly_utils::rs_encode(current_domain, current_polynomial);
  encode_ms +=
      ElapsedMilliseconds(initial_encode_start, std::chrono::steady_clock::now());

  const auto initial_commit_start = std::chrono::steady_clock::now();
  auto current_actual_tree =
      stir_whir_gr::fri::build_oracle_tree(params_.hash_profile, ctx, current_actual_oracle,
                                   params_.virtual_fold_factor);
  merkle_ms +=
      ElapsedMilliseconds(initial_commit_start, std::chrono::steady_clock::now());
  proof.initial_root = current_actual_tree.root();
  commit_ms +=
      ElapsedMilliseconds(initial_commit_start, std::chrono::steady_clock::now());

  const auto initial_transcript_start = std::chrono::steady_clock::now();
  transcript.absorb_bytes(proof.initial_root);
  auto current_folding_alpha = DeriveFoldingChallenge(
      params_, transcript, ctx, RoundLabel("stir.fold_alpha", 0));
  transcript_ms += ElapsedMilliseconds(initial_transcript_start,
                                       std::chrono::steady_clock::now());

  for (std::size_t round_index = 0; round_index < round_count; ++round_index) {
    const auto effective_query_count =
        query_metadata[round_index].effective_query_count;
    const Domain folded_domain =
        current_domain.pow_map(params_.virtual_fold_factor);
    const Domain shift_domain = current_domain.scale_offset(params_.shift_power);
    const std::uint64_t next_degree_bound =
        folded_degree_bound(current_degree_bound, params_.virtual_fold_factor);

    StirRoundProof round;
    StirRoundWitness round_witness;
    round_witness.input_polynomial = current_polynomial;

    std::vector<stir_whir_gr::algebra::GRElem> current_function_oracle;
    const auto encode_start = std::chrono::steady_clock::now();
    if (round_index == 0U) {
      current_function_oracle = current_actual_oracle;
    } else {
      current_function_oracle =
          stir_whir_gr::poly_utils::rs_encode(current_domain, current_polynomial);
    }
    encode_ms += ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());

    const auto fold_start = std::chrono::steady_clock::now();
    const auto folded_table = stir_whir_gr::poly_utils::fold_table_k(
        current_domain, current_function_oracle, params_.virtual_fold_factor,
        current_folding_alpha);
    fold_ms += ElapsedMilliseconds(fold_start, std::chrono::steady_clock::now());

    const auto interpolate_start = std::chrono::steady_clock::now();
    round_witness.folded_polynomial =
        stir_whir_gr::poly_utils::rs_interpolate(folded_domain, folded_table);
    interpolate_ms +=
        ElapsedMilliseconds(interpolate_start, std::chrono::steady_clock::now());

    const auto g_encode_start = std::chrono::steady_clock::now();
    round_witness.shifted_oracle_evals =
        stir_whir_gr::poly_utils::rs_encode(shift_domain, round_witness.folded_polynomial);
    encode_ms += ElapsedMilliseconds(g_encode_start, std::chrono::steady_clock::now());

    const auto commit_start = std::chrono::steady_clock::now();
    auto g_tree = stir_whir_gr::fri::build_oracle_tree(
        params_.hash_profile, ctx, round_witness.shifted_oracle_evals,
        params_.virtual_fold_factor);
    round.g_root = g_tree.root();
    merkle_ms += ElapsedMilliseconds(commit_start, std::chrono::steady_clock::now());

    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(round.g_root);
    round.betas = derive_ood_points(
        params_, current_domain, shift_domain, folded_domain, transcript,
        RoundLabel("stir.ood", round_index), params_.ood_samples);
    std::vector<stir_whir_gr::algebra::GRElem> ood_points = round.betas;
    round.betas.clear();
    round.betas.reserve(ood_points.size());
    const auto ood_start = std::chrono::steady_clock::now();
    for (const auto& point : ood_points) {
      round.betas.push_back(round_witness.folded_polynomial.evaluate(ctx, point));
      transcript.absorb_ring(ctx, round.betas.back());
    }
    ood_ms += ElapsedMilliseconds(ood_start, std::chrono::steady_clock::now());
    const auto comb_randomness = DeriveCombChallenge(
        params_, transcript, ctx, RoundLabel("stir.comb", round_index));
    const auto next_folding_alpha = DeriveFoldingChallenge(
        params_, transcript, ctx, RoundLabel("stir.fold_alpha", round_index + 1U));
    const auto query_positions = SortedPositions(derive_unique_positions(
        transcript, RoundLabel("stir.query", round_index), folded_domain.size(),
        effective_query_count));

    std::vector<stir_whir_gr::algebra::GRElem> quotient_points = ood_points;
    std::vector<stir_whir_gr::algebra::GRElem> quotient_values = round.betas;
    quotient_points.reserve(quotient_points.size() + query_positions.size());
    quotient_values.reserve(quotient_values.size() + query_positions.size());
    for (const auto position : query_positions) {
      quotient_points.push_back(folded_domain.element(position));
      quotient_values.push_back(
          folded_table[static_cast<std::size_t>(position)]);
    }
    const auto shake_point = derive_shake_point(
        params_, current_domain, shift_domain, folded_domain, quotient_points,
        transcript, RoundLabel("stir.shake", round_index));
    (void)shake_point;
    transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());
    commit_ms += ElapsedMilliseconds(commit_start, std::chrono::steady_clock::now());

    const auto open_start = std::chrono::steady_clock::now();
    round.queries_to_prev = current_actual_tree.open(query_positions);
    const double open_elapsed =
        ElapsedMilliseconds(open_start, std::chrono::steady_clock::now());
    query_open_ms += open_elapsed;
    query_phase_ms += open_elapsed;

    const auto answer_start = std::chrono::steady_clock::now();
    round.ans_polynomial =
        stir_whir_gr::poly_utils::answer_polynomial(ctx, quotient_points, quotient_values);
    round.shake_polynomial = BuildShakePolynomial(
        ctx, round.ans_polynomial, quotient_points, quotient_values);
    round_witness.answer_polynomial = round.ans_polynomial;
    round_witness.vanishing_polynomial =
        stir_whir_gr::poly_utils::vanishing_polynomial(ctx, quotient_points);
    answer_ms += ElapsedMilliseconds(answer_start, std::chrono::steady_clock::now());

    const auto quotient_start = std::chrono::steady_clock::now();
    round_witness.quotient_polynomial =
        stir_whir_gr::poly_utils::quotient_polynomial_from_answers(
            ctx, round_witness.folded_polynomial, quotient_points,
            quotient_values);
    quotient_ms +=
        ElapsedMilliseconds(quotient_start, std::chrono::steady_clock::now());

    const std::uint64_t quotient_degree_bound =
        SaturatingSubtract(next_degree_bound,
                           static_cast<std::uint64_t>(quotient_points.size()));
    const auto degree_correction_start = std::chrono::steady_clock::now();
    round_witness.next_polynomial = stir_whir_gr::poly_utils::degree_correction_polynomial(
        ctx, round_witness.quotient_polynomial, next_degree_bound,
        quotient_degree_bound, comb_randomness);
    degree_correction_ms += ElapsedMilliseconds(
        degree_correction_start, std::chrono::steady_clock::now());
    if (round_witness.next_polynomial.degree() > next_degree_bound) {
      throw std::runtime_error(
          "stir::StirProver::prove degree correction exceeded target degree");
    }

    proof.rounds.push_back(round);
    witness.rounds.push_back(std::move(round_witness));
    current_domain = shift_domain;
    current_degree_bound = next_degree_bound;
    current_polynomial = witness.rounds.back().next_polynomial;
    current_actual_oracle = witness.rounds.back().shifted_oracle_evals;
    current_actual_tree = std::move(g_tree);
    current_folding_alpha = next_folding_alpha;
  }

  const Domain final_domain =
      current_domain.pow_map(params_.virtual_fold_factor);
  const std::uint64_t final_degree_bound =
      folded_degree_bound(current_degree_bound, params_.virtual_fold_factor);

  const auto final_encode_start = std::chrono::steady_clock::now();
  const auto final_function_oracle =
      stir_whir_gr::poly_utils::rs_encode(current_domain, current_polynomial);
  encode_ms +=
      ElapsedMilliseconds(final_encode_start, std::chrono::steady_clock::now());

  const auto final_fold_start = std::chrono::steady_clock::now();
  const auto final_folded_table = stir_whir_gr::poly_utils::fold_table_k(
      current_domain, final_function_oracle, params_.virtual_fold_factor,
      current_folding_alpha);
  fold_ms += ElapsedMilliseconds(final_fold_start, std::chrono::steady_clock::now());

  const auto final_interpolate_start = std::chrono::steady_clock::now();
  proof.final_polynomial =
      stir_whir_gr::poly_utils::rs_interpolate(final_domain, final_folded_table);
  interpolate_ms += ElapsedMilliseconds(final_interpolate_start,
                                        std::chrono::steady_clock::now());
  if (proof.final_polynomial.degree() > final_degree_bound) {
    throw std::runtime_error(
        "stir::StirProver::prove terminal polynomial violates degree bound");
  }

  const auto final_transcript_start = std::chrono::steady_clock::now();
  const auto final_query_count = ResolveFinalQueryCount(
      params_, final_domain.size(), final_degree_bound, round_count);
  const auto final_queries = SortedPositions(derive_unique_positions(
      transcript, RoundLabel("stir.final_query", round_count), final_domain.size(),
      final_query_count));
  transcript_ms += ElapsedMilliseconds(final_transcript_start,
                                       std::chrono::steady_clock::now());

  const auto final_open_start = std::chrono::steady_clock::now();
  proof.queries_to_final = current_actual_tree.open(final_queries);
  const double final_open_elapsed =
      ElapsedMilliseconds(final_open_start, std::chrono::steady_clock::now());
  query_open_ms += final_open_elapsed;
  query_phase_ms += final_open_elapsed;

  proof.stats.prover_rounds = static_cast<std::uint64_t>(round_count);
  proof.stats.serialized_bytes = serialized_message_bytes(ctx, proof);
  proof.stats.verifier_hashes =
      static_cast<std::uint64_t>(proof.queries_to_final.sibling_hashes.size());
  for (const auto& round : proof.rounds) {
    proof.stats.verifier_hashes +=
        static_cast<std::uint64_t>(round.queries_to_prev.sibling_hashes.size());
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

}  // namespace stir_whir_gr::stir
