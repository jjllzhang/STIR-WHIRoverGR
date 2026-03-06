#include "stir/prover.hpp"

#include <cstddef>
#include <cstdint>
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

}  // namespace

StirProver::StirProver(StirParameters params) : params_(std::move(params)) {}

StirProof StirProver::prove(
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
  StirProof proof;
  Domain current_domain = instance.domain;
  std::uint64_t current_degree_bound = instance.claimed_degree;
  swgr::poly_utils::Polynomial current_polynomial = polynomial;

  const std::size_t round_count = folding_round_count(instance, params_);
  const auto schedule =
      swgr::fri::query_schedule(round_count, params_.query_repetitions);
  const std::uint64_t elem_bytes =
      static_cast<std::uint64_t>(ctx.elem_bytes());
  std::uint64_t serialized_bytes = 0;

  for (std::size_t round_index = 0; round_index < round_count; ++round_index) {
    StirRoundProof round;
    round.round_index = static_cast<std::uint64_t>(round_index);
    round.input_domain_size = current_domain.size();
    round.input_degree_bound = current_degree_bound;
    round.input_polynomial = current_polynomial;

    const auto input_oracle =
        swgr::poly_utils::rs_encode(current_domain, current_polynomial);
    const auto input_root = swgr::fri::commit_oracle(ctx, input_oracle);
    round.folding_alpha = swgr::fri::derive_round_challenge(
        ctx, input_root, static_cast<std::uint64_t>(round_index));

    const Domain folded_domain =
        current_domain.pow_map(params_.virtual_fold_factor);
    const Domain shift_domain = current_domain.scale_offset(params_.shift_power);
    round.folded_domain_size = folded_domain.size();
    round.shift_domain_size = shift_domain.size();
    round.folded_degree_bound =
        folded_degree_bound(current_degree_bound, params_.virtual_fold_factor);

    const auto folded_table = swgr::poly_utils::fold_table_k(
        current_domain, input_oracle, params_.virtual_fold_factor,
        round.folding_alpha);
    round.folded_polynomial =
        swgr::poly_utils::rs_interpolate(folded_domain, folded_table);
    round.shifted_oracle_evals =
        swgr::poly_utils::rs_encode(shift_domain, round.folded_polynomial);

    const auto oracle_root =
        swgr::fri::commit_oracle(ctx, round.shifted_oracle_evals);
    proof.oracle_roots.push_back(oracle_root);

    round.ood_points = derive_ood_points(
        current_domain, shift_domain, folded_domain, oracle_root,
        static_cast<std::uint64_t>(round_index), params_.ood_samples);
    round.ood_answers.reserve(round.ood_points.size());
    for (const auto& point : round.ood_points) {
      round.ood_answers.push_back(round.folded_polynomial.evaluate(ctx, point));
    }

    const auto combination_seed =
        AppendSerializedElements(ctx, oracle_root, round.ood_answers);
    round.comb_randomness = swgr::fri::derive_round_challenge(
        ctx, combination_seed,
        static_cast<std::uint64_t>(round_index) + kCombinationTagOffset);

    const auto shift_seed =
        AppendSerializedElement(ctx, combination_seed, round.comb_randomness);
    round.shift_query_positions = derive_unique_positions(
        shift_seed, static_cast<std::uint64_t>(round_index) + kShiftTagOffset,
        folded_domain.size(), schedule[round_index]);
    round.shift_query_answers.reserve(round.shift_query_positions.size());

    std::vector<swgr::algebra::GRElem> answer_points = round.ood_points;
    std::vector<swgr::algebra::GRElem> answer_values = round.ood_answers;
    answer_points.reserve(answer_points.size() + round.shift_query_positions.size());
    answer_values.reserve(answer_values.size() +
                          round.shift_query_positions.size());
    for (const auto position : round.shift_query_positions) {
      round.shift_query_answers.push_back(
          folded_table[static_cast<std::size_t>(position)]);
      answer_points.push_back(folded_domain.element(position));
      answer_values.push_back(round.shift_query_answers.back());
    }

    round.answer_polynomial =
        swgr::poly_utils::answer_polynomial(ctx, answer_points, answer_values);
    round.vanishing_polynomial =
        swgr::poly_utils::vanishing_polynomial(ctx, answer_points);
    round.quotient_polynomial = swgr::poly_utils::quotient_polynomial_from_answers(
        ctx, round.folded_polynomial, answer_points, answer_values);

    const std::uint64_t quotient_degree_bound =
        SaturatingSubtract(round.folded_degree_bound, answer_points.size());
    round.next_polynomial = swgr::poly_utils::degree_correction_polynomial(
        ctx, round.quotient_polynomial, round.folded_degree_bound,
        quotient_degree_bound, round.comb_randomness);
    if (round.next_polynomial.degree() > round.folded_degree_bound) {
      throw std::runtime_error(
          "stir::StirProver::prove degree correction exceeded target degree");
    }

    serialized_bytes +=
        static_cast<std::uint64_t>(round.shifted_oracle_evals.size()) * elem_bytes;
    serialized_bytes +=
        static_cast<std::uint64_t>(round.ood_answers.size() +
                                   round.shift_query_answers.size()) *
        elem_bytes;
    serialized_bytes +=
        static_cast<std::uint64_t>(round.answer_polynomial.coefficients().size() +
                                   round.vanishing_polynomial.coefficients().size() +
                                   round.quotient_polynomial.coefficients().size() +
                                   round.next_polynomial.coefficients().size() +
                                   round.input_polynomial.coefficients().size() +
                                   round.folded_polynomial.coefficients().size()) *
        elem_bytes;
    serialized_bytes += static_cast<std::uint64_t>(oracle_root.size());
    serialized_bytes +=
        static_cast<std::uint64_t>(round.shift_query_positions.size()) *
        sizeof(std::uint64_t);

    proof.rounds.push_back(round);
    current_domain = shift_domain;
    current_degree_bound = proof.rounds.back().folded_degree_bound;
    current_polynomial = proof.rounds.back().next_polynomial;
  }

  proof.final_polynomial = current_polynomial;
  if (proof.final_polynomial.degree() > current_degree_bound) {
    throw std::runtime_error(
        "stir::StirProver::prove terminal polynomial violates degree bound");
  }
  serialized_bytes +=
      static_cast<std::uint64_t>(proof.final_polynomial.coefficients().size()) *
      elem_bytes;

  proof.stats.prover_rounds = static_cast<std::uint64_t>(round_count);
  proof.stats.serialized_bytes = serialized_bytes;
  proof.stats.verifier_hashes = 0;
  return proof;
}

}  // namespace swgr::stir
