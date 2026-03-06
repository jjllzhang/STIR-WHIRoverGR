#include "stir/verifier.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "fri/common.hpp"
#include "poly_utils/degree_correction.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/quotient.hpp"

namespace swgr::stir {
namespace {

constexpr std::uint64_t kCombinationTagOffset = 0x20000ULL;
constexpr std::uint64_t kShiftTagOffset = 0x30000ULL;

std::vector<std::uint8_t> AppendSerializedElement(
    const swgr::algebra::GRContext& ctx, std::vector<std::uint8_t> seed,
    const swgr::algebra::GRElem& value) {
  const auto serialized = ctx.serialize(value);
  seed.insert(seed.end(), serialized.begin(), serialized.end());
  return seed;
}

std::vector<std::uint8_t> AppendSerializedElements(
    const swgr::algebra::GRContext& ctx, std::vector<std::uint8_t> seed,
    const std::vector<swgr::algebra::GRElem>& values) {
  for (const auto& value : values) {
    seed = AppendSerializedElement(ctx, std::move(seed), value);
  }
  return seed;
}

std::uint64_t SaturatingSubtract(std::uint64_t lhs, std::uint64_t rhs) {
  return lhs >= rhs ? lhs - rhs : 0;
}

bool SamePolynomial(const swgr::poly_utils::Polynomial& lhs,
                    const swgr::poly_utils::Polynomial& rhs) {
  return lhs.coefficients() == rhs.coefficients();
}

template <typename T>
bool SameVector(const std::vector<T>& lhs, const std::vector<T>& rhs) {
  return lhs == rhs;
}

}  // namespace

StirVerifier::StirVerifier(StirParameters params) : params_(std::move(params)) {}

bool StirVerifier::verify(const StirInstance& instance,
                         const StirProof& proof) const {
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

    const auto schedule =
        swgr::fri::query_schedule(round_count, params_.query_repetitions);
    Domain current_domain = instance.domain;
    std::uint64_t current_degree_bound = instance.claimed_degree;
    swgr::poly_utils::Polynomial expected_current_polynomial =
        proof.rounds.empty() ? proof.final_polynomial
                             : proof.rounds.front().input_polynomial;

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
      const auto input_oracle =
          swgr::poly_utils::rs_encode(current_domain, round.input_polynomial);
      const auto input_root = swgr::fri::commit_oracle(ctx, input_oracle);
      const auto expected_alpha = swgr::fri::derive_round_challenge(
          ctx, input_root, static_cast<std::uint64_t>(round_index));
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

      const auto folded_table = swgr::poly_utils::fold_table_k(
          current_domain, input_oracle, params_.virtual_fold_factor,
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
      const auto oracle_root =
          swgr::fri::commit_oracle(ctx, expected_shifted_oracle);
      if (oracle_root != proof.oracle_roots[round_index]) {
        return false;
      }

      const auto expected_ood_points = derive_ood_points(
          current_domain, shift_domain, folded_domain, oracle_root,
          static_cast<std::uint64_t>(round_index), params_.ood_samples);
      if (!SameVector(round.ood_points, expected_ood_points)) {
        return false;
      }
      std::vector<swgr::algebra::GRElem> expected_ood_answers;
      expected_ood_answers.reserve(expected_ood_points.size());
      for (const auto& point : expected_ood_points) {
        expected_ood_answers.push_back(
            expected_folded_polynomial.evaluate(ctx, point));
      }
      if (!SameVector(round.ood_answers, expected_ood_answers)) {
        return false;
      }

      const auto combination_seed =
          AppendSerializedElements(ctx, oracle_root, expected_ood_answers);
      const auto expected_combination = swgr::fri::derive_round_challenge(
          ctx, combination_seed,
          static_cast<std::uint64_t>(round_index) + kCombinationTagOffset);
      if (round.comb_randomness != expected_combination) {
        return false;
      }

      const auto shift_seed =
          AppendSerializedElement(ctx, combination_seed, expected_combination);
      const auto expected_shift_positions = derive_unique_positions(
          shift_seed, static_cast<std::uint64_t>(round_index) + kShiftTagOffset,
          folded_domain.size(), schedule[round_index]);
      if (round.shift_query_positions != expected_shift_positions) {
        return false;
      }

      std::vector<swgr::algebra::GRElem> expected_shift_answers;
      expected_shift_answers.reserve(expected_shift_positions.size());
      std::vector<swgr::algebra::GRElem> answer_points = expected_ood_points;
      std::vector<swgr::algebra::GRElem> answer_values = expected_ood_answers;
      for (const auto position : expected_shift_positions) {
        expected_shift_answers.push_back(
            folded_table[static_cast<std::size_t>(position)]);
        answer_points.push_back(folded_domain.element(position));
        answer_values.push_back(expected_shift_answers.back());
      }
      if (!SameVector(round.shift_query_answers, expected_shift_answers)) {
        return false;
      }

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

      expected_current_polynomial = expected_next_polynomial;
      current_domain = shift_domain;
      current_degree_bound = next_degree_bound;
    }

    return SamePolynomial(proof.final_polynomial, expected_current_polynomial) &&
           proof.final_polynomial.degree() <= current_degree_bound;
  } catch (...) {
    return false;
  }
}

}  // namespace swgr::stir
