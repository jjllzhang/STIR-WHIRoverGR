#include "whir/prover.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "crypto/fs/transcript.hpp"
#include "utils.hpp"
#include "whir/constraint.hpp"
#include "whir/folding.hpp"

namespace swgr::whir {
namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

std::vector<swgr::algebra::GRElem> EncodeOracle(
    const swgr::algebra::GRContext& ctx, const Domain& domain,
    const MultiQuadraticPolynomial& polynomial) {
  std::vector<swgr::algebra::GRElem> oracle;
  oracle.reserve(static_cast<std::size_t>(domain.size()));
  for (std::uint64_t index = 0; index < domain.size(); ++index) {
    oracle.push_back(polynomial.evaluate_pow(ctx, domain.element(index)));
  }
  return oracle;
}

std::vector<swgr::algebra::GRElem> EncodeInitialOracle(
    const WhirPublicParameters& pp,
    const MultiQuadraticPolynomial& polynomial) {
  return EncodeOracle(*pp.ctx, pp.initial_domain, polynomial);
}

std::vector<std::uint64_t> SortedUnique(std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

}  // namespace

WhirProver::WhirProver(WhirParameters params) : params_(std::move(params)) {}

WhirCommitment WhirProver::commit(
    const WhirPublicParameters& pp,
    const MultiQuadraticPolynomial& polynomial,
    WhirCommitmentState* state) const {
  if (!validate(params_, pp)) {
    throw std::invalid_argument(
        "whir::WhirProver::commit received invalid public parameters");
  }
  if (polynomial.variable_count() != pp.variable_count) {
    throw std::invalid_argument(
        "whir::WhirProver::commit polynomial variable count mismatch");
  }

  const auto commit_start = std::chrono::steady_clock::now();
  const auto encode_start = std::chrono::steady_clock::now();
  auto initial_oracle = EncodeInitialOracle(pp, polynomial);
  const double encode_ms =
      ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());

  const auto merkle_start = std::chrono::steady_clock::now();
  auto tree = build_oracle_tree(pp.hash_profile, *pp.ctx, initial_oracle);
  const double merkle_ms =
      ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());

  WhirCommitment commitment{
      .public_params = pp,
      .oracle_root = tree.root(),
      .stats = {},
  };
  commitment.stats.prover_encode_ms = encode_ms;
  commitment.stats.prover_merkle_ms = merkle_ms;
  commitment.stats.commit_ms =
      ElapsedMilliseconds(commit_start, std::chrono::steady_clock::now());
  commitment.stats.prover_total_ms = commitment.stats.commit_ms;
  commitment.stats.serialized_bytes = serialized_message_bytes(commitment);

  if (!validate(params_, commitment)) {
    throw std::runtime_error(
        "whir::WhirProver::commit produced an invalid commitment");
  }

  if (state != nullptr) {
    state->public_params = pp;
    state->polynomial = polynomial;
    state->initial_oracle = std::move(initial_oracle);
    state->oracle_root = commitment.oracle_root;
  }
  return commitment;
}

WhirOpening WhirProver::open(
    const WhirCommitment& commitment, const WhirCommitmentState& state,
    std::span<const swgr::algebra::GRElem> point) const {
  if (!validate(params_, commitment)) {
    throw std::invalid_argument(
        "whir::WhirProver::open received invalid commitment");
  }
  if (!state.public_params || !state.polynomial) {
    throw std::invalid_argument(
        "whir::WhirProver::open requires commit state produced by commit");
  }

  const auto& pp = commitment.public_params;
  const auto& ctx = *pp.ctx;
  if (point.size() != static_cast<std::size_t>(pp.variable_count)) {
    throw std::invalid_argument(
        "whir::WhirProver::open point length mismatch");
  }
  if (state.oracle_root != commitment.oracle_root ||
      state.initial_oracle.size() !=
          static_cast<std::size_t>(pp.initial_domain.size())) {
    throw std::invalid_argument(
        "whir::WhirProver::open state does not match commitment");
  }

  auto current_tree =
      build_oracle_tree(pp.hash_profile, ctx, state.initial_oracle);
  if (current_tree.root() != commitment.oracle_root) {
    throw std::invalid_argument(
        "whir::WhirProver::open state oracle does not match commitment root");
  }
  if (pp.final_repetitions == 0) {
    throw std::invalid_argument(
        "whir::WhirProver::open requires final_repetitions > 0");
  }

  const auto open_start = std::chrono::steady_clock::now();
  auto current_polynomial = *state.polynomial;
  auto current_oracle = state.initial_oracle;
  auto current_domain = pp.initial_domain;

  WhirOpening opening;
  opening.value = current_polynomial.evaluate(ctx, point);
  opening.proof.rounds.reserve(pp.layer_widths.size());

  swgr::crypto::Transcript transcript(pp.hash_profile);
  absorb_opening_preamble(transcript, commitment, point, opening.value);

  WhirConstraint constraint(pp.ternary_grid);
  constraint.add_shift_term(ctx.one(),
                            std::vector<swgr::algebra::GRElem>(point.begin(),
                                                                point.end()));
  auto sigma = opening.value;

  for (std::size_t layer = 0; layer < pp.layer_widths.size(); ++layer) {
    const std::uint64_t width = pp.layer_widths[layer];
    WhirRoundProof round;
    round.sumcheck_polynomials.reserve(static_cast<std::size_t>(width));

    std::vector<swgr::algebra::GRElem> alphas;
    alphas.reserve(static_cast<std::size_t>(width));
    for (std::uint64_t j = 0; j < width; ++j) {
      const auto h =
          honest_sumcheck_polynomial(ctx, current_polynomial, constraint, alphas);
      round.sumcheck_polynomials.push_back(h);
      absorb_sumcheck_polynomial(transcript, ctx,
                                 round.sumcheck_polynomials.back());
      const auto alpha = transcript.challenge_teichmuller(
          ctx, indexed_label(kTranscriptLabelAlpha,
                             static_cast<std::uint64_t>(layer), j));
      sigma = sumcheck_next_sigma(ctx, h, alpha);
      alphas.push_back(alpha);
    }

    auto next_polynomial = current_polynomial.restrict_prefix(ctx, alphas);
    auto next_domain = current_domain.pow_map(3U);
    auto next_oracle = EncodeOracle(ctx, next_domain, next_polynomial);
    auto next_tree = build_oracle_tree(pp.hash_profile, ctx, next_oracle);
    round.g_root = next_tree.root();
    transcript.absorb_bytes(round.g_root);

    const std::uint64_t shift_domain_size =
        current_domain.size() / pow3_checked(width);
    const auto shift_positions = derive_unique_positions(
        transcript,
        indexed_label(kTranscriptLabelShift,
                      static_cast<std::uint64_t>(layer)),
        shift_domain_size, pp.shift_repetitions[layer]);
    auto folded_for_queries =
        repeated_ternary_fold_table(current_domain, current_oracle, alphas);

    std::vector<std::uint64_t> parent_indices;
    parent_indices.reserve(shift_positions.size() *
                           static_cast<std::size_t>(pow3_checked(width)));
    const Domain shift_domain = current_domain.pow_map(pow3_checked(width));
    std::vector<std::vector<swgr::algebra::GRElem>> shift_points;
    std::vector<swgr::algebra::GRElem> shift_values;
    shift_points.reserve(shift_positions.size());
    shift_values.reserve(shift_positions.size());
    for (const auto shift_index : shift_positions) {
      const auto indices =
          virtual_fold_query_indices(current_domain.size(), width, shift_index);
      parent_indices.insert(parent_indices.end(), indices.begin(), indices.end());
      shift_values.push_back(folded_for_queries[static_cast<std::size_t>(
          shift_index)]);
      shift_points.push_back(
          pow_m(ctx, shift_domain.element(shift_index),
                next_polynomial.variable_count()));
    }
    round.virtual_fold_openings =
        current_tree.open(SortedUnique(std::move(parent_indices)));

    const auto gamma = transcript.challenge_teichmuller(
        ctx, indexed_label(kTranscriptLabelGamma,
                           static_cast<std::uint64_t>(layer)));
    auto next_constraint = constraint.restrict_prefix(ctx, alphas);
    ctx.with_ntl_context([&] {
      auto gamma_power = gamma;
      for (std::size_t i = 0; i < shift_values.size(); ++i) {
        next_constraint.add_shift_term(gamma_power, shift_points[i]);
        sigma += gamma_power * shift_values[i];
        gamma_power *= gamma;
      }
      return 0;
    });

    opening.proof.rounds.push_back(std::move(round));
    current_polynomial = std::move(next_polynomial);
    current_domain = std::move(next_domain);
    current_oracle = std::move(next_oracle);
    current_tree = std::move(next_tree);
    constraint = std::move(next_constraint);
  }

  opening.proof.final_constant = ctx.with_ntl_context([&] {
    const std::vector<swgr::algebra::GRElem> empty_point;
    return current_polynomial.evaluate(ctx, empty_point);
  });
  transcript.absorb_ring(ctx, opening.proof.final_constant);
  const auto final_positions = derive_unique_positions(
      transcript, kTranscriptLabelFinalQuery, current_domain.size(),
      pp.final_repetitions);
  opening.proof.final_openings = current_tree.open(final_positions);

  opening.proof.stats.prover_rounds =
      static_cast<std::uint64_t>(opening.proof.rounds.size());
  opening.proof.stats.serialized_bytes = serialized_message_bytes(ctx, opening);
  opening.proof.stats.prover_total_ms =
      ElapsedMilliseconds(open_start, std::chrono::steady_clock::now());
  opening.proof.stats.prove_query_phase_ms =
      opening.proof.stats.prover_total_ms;
  return opening;
}

WhirProof WhirProver::prove() const {
  (void)params_;
  throw_unimplemented("whir::WhirProver::prove");
}

}  // namespace swgr::whir
