#include "stir/soundness.hpp"

#include <NTL/ZZ.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "algebra/teichmuller.hpp"
#include "fri/common.hpp"
#include "soundness/configurator.hpp"
#include "stir/common.hpp"

namespace swgr::stir {
namespace {

long double ProbabilityFromRatio(const NTL::ZZ& numerator,
                                 const NTL::ZZ& denominator) {
  if (denominator <= 0 || numerator <= 0) {
    return 0.0L;
  }
  if (numerator >= denominator) {
    return 1.0L;
  }
  return std::exp(std::log(static_cast<long double>(NTL::conv<double>(numerator))) -
                   std::log(static_cast<long double>(NTL::conv<double>(denominator))));
}

long double ClampProbability(long double value) {
  if (!std::isfinite(value) || value <= 0.0L) {
    return 0.0L;
  }
  if (value >= 1.0L) {
    return 1.0L;
  }
  return value;
}

long double HalfGapFoldEnvelope(const NTL::ZZ& teich_size,
                                std::uint64_t domain_size,
                                std::uint64_t combination_arity) {
  NTL::ZZ numerator = teich_size;
  numerator = 0;
  numerator += combination_arity;
  numerator *= domain_size;
  numerator *= domain_size;
  return ClampProbability(ProbabilityFromRatio(numerator, teich_size));
}

long double HalfGapDegreeEnvelope(const NTL::ZZ& teich_size,
                                  std::uint64_t effective_combination_arity,
                                  std::uint64_t domain_size) {
  NTL::ZZ numerator(0);
  numerator += effective_combination_arity;
  numerator *= domain_size;
  numerator *= domain_size;
  return ClampProbability(ProbabilityFromRatio(numerator, teich_size));
}

bool DomainSupportsHalfGap(std::uint64_t domain_size,
                           std::uint64_t combination_arity,
                           const NTL::ZZ& teich_size) {
  NTL::ZZ numerator(0);
  numerator += combination_arity;
  numerator *= domain_size;
  numerator *= domain_size;
  return numerator < teich_size;
}

long double UniqueQueryMissProbability(long double delta,
                                       std::uint64_t query_count) {
  if (query_count == 0) {
    return 1.0L;
  }
  const long double miss_base = ClampProbability(1.0L - delta);
  if (miss_base == 0.0L) {
    return 0.0L;
  }
  if (miss_base == 1.0L) {
    return 1.0L;
  }
  return ClampProbability(
      std::exp(static_cast<long double>(query_count) * std::log(miss_base)));
}

std::uint64_t SecurityBitsFromError(long double epsilon) {
  if (!(epsilon > 0.0L) || epsilon >= 1.0L) {
    return 0;
  }
  const long double bits = -std::log2(epsilon);
  if (!std::isfinite(bits) || bits <= 0.0L) {
    return 0;
  }
  if (bits >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(std::floor(bits));
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

}  // namespace

StirTheoremSoundnessAnalysis analyze_theorem_soundness(
    const StirParameters& params, const StirInstance& instance) {
  StirTheoremSoundnessAnalysis analysis;
  analysis.proximity_gap_model =
      "z2ksnark_gr_half_gap_envelope_s_times_ell_sq_over_T";
  analysis.ood_model = "unique_decoding_exceptional_complement";
  analysis.assumptions.push_back(
      "Theorem analysis only applies to theorem_gr mode.");
  analysis.assumptions.push_back(
      "Unique-decoding regime requires delta_i <= (1-rho_i)/2 in every round.");
  analysis.assumptions.push_back(
      "GR half-gap folding term uses min(1, m*ell^2/|T|) from the existing Z2KSNARK (1-rho)/2 proximity result.");
  analysis.assumptions.push_back(
      "GR half-gap degree-correction term uses min(1, m*ell^2/|T|) with m = t_i + s_ood under the same Z2KSNARK half-gap regime.");
  analysis.assumptions.push_back(
      "epsilon_out is fixed to 0 because theorem-mode OOD stays in the unique-decoding regime.");

  if (params.protocol_mode != StirProtocolMode::TheoremGr ||
      !validate(params, instance)) {
    analysis.assumptions.push_back(
        "Unsupported: theorem soundness requires a theorem-mode parameter set that already passes theorem validation.");
    return analysis;
  }

  const auto teich_size = swgr::algebra::teichmuller_set_size(instance.domain.context());
  const auto schedule = resolve_query_schedule_metadata(params, instance);
  const std::size_t round_count = folding_round_count(instance, params);
  if (schedule.size() != round_count) {
    analysis.assumptions.push_back(
        "Unsupported: theorem soundness could not reconstruct the live STIR query schedule.");
    return analysis;
  }

  Domain current_domain = instance.domain;
  std::uint64_t current_degree_bound = instance.claimed_degree;

  if (!DomainSupportsHalfGap(current_domain.size(),
                             params.virtual_fold_factor, teich_size)) {
    analysis.assumptions.push_back(
        "Unsupported: initial domain size is too large for the theorem_gr half-gap folding regime to remain non-trivial.");
    return analysis;
  }

  analysis.epsilon_fold = HalfGapFoldEnvelope(
      teich_size, current_domain.size(), params.virtual_fold_factor);
  if (analysis.epsilon_fold >= 1.0L) {
    analysis.assumptions.push_back(
        "Unsupported: theorem_gr half-gap folding bound is trivial for the initial domain.");
    return analysis;
  }

  long double total_error = analysis.epsilon_fold;
  for (std::size_t round_index = 0; round_index < round_count; ++round_index) {
    const auto& round = schedule[round_index];
    const Domain folded_domain = current_domain.pow_map(params.virtual_fold_factor);
    const Domain shift_domain = current_domain.scale_offset(params.shift_power);
    const std::uint64_t next_degree_bound =
        folded_degree_bound(current_degree_bound, params.virtual_fold_factor);
    const long double rho =
        static_cast<long double>(current_degree_bound + 1U) /
        static_cast<long double>(current_domain.size());
    const long double delta = 0.5L * (1.0L - rho);

    StirRoundTheoremSoundnessTerm term;
    term.round_index = round_index;
    term.degree_bound = current_degree_bound;
    term.domain_size = current_domain.size();
    term.effective_query_count = round.effective_query_count;
    term.epsilon_out = 0.0L;

    if (!(delta > 0.0L)) {
      term.notes.push_back(
          "Unsupported: unique-decoding gap delta_i is non-positive for this round.");
      analysis.rounds.push_back(term);
      analysis.assumptions.push_back(
          "Unsupported: at least one round leaves the unique-decoding theorem regime.");
      return analysis;
    }
    const std::uint64_t degree_arity =
        round.effective_query_count + params.ood_samples;
    if (!DomainSupportsHalfGap(term.domain_size, params.virtual_fold_factor,
                               teich_size)) {
      term.notes.push_back(
          "Unsupported: round domain is too large for the theorem_gr half-gap folding regime.");
      analysis.rounds.push_back(term);
      analysis.assumptions.push_back(
          "Unsupported: at least one round violates the theorem_gr half-gap folding size guard k*ell_i^2 < |T|.");
      return analysis;
    }
    if (!DomainSupportsHalfGap(term.domain_size, degree_arity, teich_size)) {
      term.notes.push_back(
          "Unsupported: round domain is too large for the theorem_gr half-gap degree-correction regime.");
      analysis.rounds.push_back(term);
      analysis.assumptions.push_back(
          "Unsupported: at least one round violates the theorem_gr half-gap degree-correction size guard (t_i+s_ood)*ell_i^2 < |T|.");
      return analysis;
    }

    const long double shift_hit =
        UniqueQueryMissProbability(delta, round.effective_query_count);
    const long double degree_term =
        HalfGapDegreeEnvelope(teich_size, degree_arity, term.domain_size);
    const long double fold_term = HalfGapFoldEnvelope(
        teich_size, term.domain_size, params.virtual_fold_factor);
    if (degree_term >= 1.0L || fold_term >= 1.0L) {
      term.notes.push_back(
          "Unsupported: theorem_gr half-gap degree or folding term is trivial in this round.");
      analysis.rounds.push_back(term);
      analysis.assumptions.push_back(
          "Unsupported: at least one round only admits a trivial theorem_gr half-gap bound.");
      return analysis;
    }

    term.epsilon_shift = ClampProbability(shift_hit + degree_term + fold_term);
    term.notes.push_back(
        "epsilon_shift = (1-delta_prev)^t_prev + theorem_gr half-gap degree term + theorem_gr half-gap folding term.");
    term.notes.push_back(
        "delta_i is instantiated as (1-rho_i)/2 under the existing Z2KSNARK half-gap result.");
    analysis.rounds.push_back(term);
    total_error += term.epsilon_shift;

    current_domain = shift_domain;
    current_degree_bound = next_degree_bound;
  }

  const std::uint64_t final_query_count = ResolveFinalQueryCount(
      params, current_domain.size(), current_degree_bound, round_count);
  const long double final_rho =
      static_cast<long double>(current_degree_bound + 1U) /
      static_cast<long double>(current_domain.size());
  const long double final_delta = 0.5L * (1.0L - final_rho);
  if (!(final_delta > 0.0L)) {
    analysis.assumptions.push_back(
        "Unsupported: final theorem round leaves the unique-decoding regime.");
    return analysis;
  }
  analysis.epsilon_fin = UniqueQueryMissProbability(final_delta, final_query_count);
  total_error += analysis.epsilon_fin;

  analysis.feasible = total_error < 1.0L;
  if (!analysis.feasible) {
    analysis.assumptions.push_back(
        "Unsupported: the combined theorem_gr half-gap envelope is trivial for this parameter set.");
    return analysis;
  }

  analysis.effective_security_bits = SecurityBitsFromError(total_error);
  return analysis;
}

}  // namespace swgr::stir
