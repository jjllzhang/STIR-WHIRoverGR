#include "whir/prover.hpp"

#include <NTL/ZZ_pE.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(STIR_WHIR_GR_HAS_OPENMP)
#include <omp.h>
#endif

#include "crypto/fs/transcript.hpp"
#include "whir/constraint.hpp"
#include "whir/folding.hpp"

using NTL::clear;
using NTL::power;

namespace stir_whir_gr::whir {
namespace {

constexpr std::uint64_t kParallelEncodeThreshold = 128U;

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

long CheckedLong(std::uint64_t value, const char* label) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

std::ptrdiff_t CheckedPtrdiff(std::uint64_t value, const char* label) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds ptrdiff_t");
  }
  return static_cast<std::ptrdiff_t>(value);
}

std::size_t CheckedSize(std::uint64_t value, const char* label) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds size_t");
  }
  return static_cast<std::size_t>(value);
}

std::ptrdiff_t ChooseEncodeChunkSize(std::uint64_t count) {
  std::uint64_t chunk_size = 1U;
#if defined(STIR_WHIR_GR_HAS_OPENMP)
  const int max_threads = omp_get_max_threads();
  if (max_threads > 0) {
    const std::uint64_t target_chunks =
        static_cast<std::uint64_t>(max_threads) * 4U;
    chunk_size =
        std::max(chunk_size, (count + target_chunks - 1U) / target_chunks);
  }
#else
  chunk_size = count;
#endif
  return CheckedPtrdiff(chunk_size, "encode chunk_size");
}

stir_whir_gr::algebra::GRElem EvaluatePowNoContext(
    const std::vector<stir_whir_gr::algebra::GRElem>& coefficients,
    const stir_whir_gr::algebra::GRElem& x) {
  stir_whir_gr::algebra::GRElem acc;
  clear(acc);
  for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
    acc *= x;
    acc += *it;
  }
  return acc;
}

std::vector<stir_whir_gr::algebra::GRElem> EncodeOracle(
    const stir_whir_gr::algebra::GRContext& ctx, const Domain& domain,
    const MultiQuadraticPolynomial& polynomial) {
  std::vector<stir_whir_gr::algebra::GRElem> oracle(
      CheckedSize(domain.size(), "domain size"));
  const std::ptrdiff_t count = CheckedPtrdiff(domain.size(), "domain size");
  const bool large_enough = domain.size() >= kParallelEncodeThreshold;
  const std::ptrdiff_t chunk_size =
      large_enough ? ChooseEncodeChunkSize(domain.size()) : count;
  const bool parallelize = large_enough && count > chunk_size;
  const auto& coefficients = polynomial.coefficients();

  ctx.parallel_for_chunks_with_ntl_context(
      count, chunk_size, parallelize,
      [&](std::ptrdiff_t begin, std::ptrdiff_t end) {
        auto x = domain.offset() *
                 power(domain.root(),
                       CheckedLong(static_cast<std::uint64_t>(begin),
                                   "encode chunk begin"));
        for (std::ptrdiff_t index = begin; index < end; ++index) {
          oracle[static_cast<std::size_t>(index)] =
              EvaluatePowNoContext(coefficients, x);
          x *= domain.root();
        }
      });
  return oracle;
}

std::vector<stir_whir_gr::algebra::GRElem> EncodeInitialOracle(
    const WhirPublicParameters& pp,
    const MultiQuadraticPolynomial& polynomial) {
  return EncodeOracle(*pp.ctx, pp.initial_domain, polynomial);
}

std::vector<std::uint64_t> SortedUnique(std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

stir_whir_gr::algebra::GRElem EvaluateVirtualFoldQueryFromOracle(
    const Domain& domain,
    const std::vector<stir_whir_gr::algebra::GRElem>& oracle,
    std::span<const std::uint64_t> indices,
    std::span<const stir_whir_gr::algebra::GRElem> alphas) {
  if (oracle.size() != domain.size()) {
    throw std::invalid_argument(
        "WHIR virtual fold query requires oracle size == domain size");
  }

  std::vector<stir_whir_gr::algebra::GRElem> points;
  std::vector<stir_whir_gr::algebra::GRElem> values;
  points.reserve(indices.size());
  values.reserve(indices.size());
  for (const std::uint64_t index : indices) {
    points.push_back(domain.element(index));
    values.push_back(oracle[static_cast<std::size_t>(index)]);
  }

  return evaluate_repeated_ternary_fold_from_values(points, values, alphas);
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
    state->initial_tree = std::move(tree);
    state->initial_oracle = std::move(initial_oracle);
    state->oracle_root = commitment.oracle_root;
  }
  return commitment;
}

WhirCommitment WhirProver::commit(
    const WhirPublicParameters& pp, const MultilinearPolynomial& polynomial,
    WhirCommitmentState* state) const {
  if (!pp.ctx) {
    throw std::invalid_argument(
        "whir::WhirProver::commit received invalid public parameters");
  }
  return commit(pp, polynomial.to_multi_quadratic(*pp.ctx), state);
}

WhirOpening WhirProver::open(
    const WhirCommitment& commitment, const WhirCommitmentState& state,
    std::span<const stir_whir_gr::algebra::GRElem> point) const {
  if (!validate(params_, commitment)) {
    throw std::invalid_argument(
        "whir::WhirProver::open received invalid commitment");
  }
  if (!state.public_params || !state.polynomial || !state.initial_tree) {
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
      state.initial_tree->root() != commitment.oracle_root ||
      state.initial_tree->leaf_count() !=
          static_cast<std::size_t>(pp.initial_domain.size()) ||
      state.initial_oracle.size() !=
          static_cast<std::size_t>(pp.initial_domain.size())) {
    throw std::invalid_argument(
        "whir::WhirProver::open state does not match commitment");
  }
  if (pp.final_repetitions == 0) {
    throw std::invalid_argument(
        "whir::WhirProver::open requires final_repetitions > 0");
  }

  const auto open_start = std::chrono::steady_clock::now();
  double encode_ms = 0.0;
  double merkle_ms = 0.0;
  double transcript_ms = 0.0;
  double fold_ms = 0.0;
  double interpolate_ms = 0.0;
  double query_open_ms = 0.0;

  auto timer_start = std::chrono::steady_clock::now();
  const stir_whir_gr::crypto::MerkleTree* current_tree =
      &(*state.initial_tree);
  std::optional<stir_whir_gr::crypto::MerkleTree> current_tree_storage;

  auto current_polynomial = *state.polynomial;
  auto current_oracle = state.initial_oracle;
  auto current_domain = pp.initial_domain;

  WhirOpening opening;
  opening.value = current_polynomial.evaluate(ctx, point);
  opening.proof.rounds.reserve(pp.layer_widths.size());

  stir_whir_gr::crypto::Transcript transcript(pp.hash_profile);
  timer_start = std::chrono::steady_clock::now();
  absorb_opening_preamble(transcript, commitment, point, opening.value);
  transcript_ms +=
      ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

  WhirConstraint constraint(pp.ternary_grid);
  constraint.add_shift_term(ctx.one(),
                            std::vector<stir_whir_gr::algebra::GRElem>(point.begin(),
                                                                point.end()));
  auto sigma = opening.value;

  for (std::size_t layer = 0; layer < pp.layer_widths.size(); ++layer) {
    const std::uint64_t width = pp.layer_widths[layer];
    WhirRoundProof round;
    round.sumcheck_polynomials.reserve(static_cast<std::size_t>(width));

    std::vector<stir_whir_gr::algebra::GRElem> alphas;
    alphas.reserve(static_cast<std::size_t>(width));
    for (std::uint64_t j = 0; j < width; ++j) {
      timer_start = std::chrono::steady_clock::now();
      const auto h =
          honest_sumcheck_polynomial(ctx, current_polynomial, constraint, alphas);
      interpolate_ms +=
          ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
      round.sumcheck_polynomials.push_back(h);
      timer_start = std::chrono::steady_clock::now();
      absorb_sumcheck_polynomial(transcript, ctx,
                                 indexed_label(kTranscriptLabelSumcheckPolynomial,
                                               static_cast<std::uint64_t>(layer),
                                               j),
                                 round.sumcheck_polynomials.back());
      const auto alpha = transcript.challenge_teichmuller(
          ctx, indexed_label(kTranscriptLabelAlpha,
                             static_cast<std::uint64_t>(layer), j));
      transcript_ms +=
          ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
      sigma = sumcheck_next_sigma(ctx, h, alpha);
      alphas.push_back(alpha);
    }

    auto next_polynomial = current_polynomial.restrict_prefix(ctx, alphas);
    auto next_domain = current_domain.pow_map(3U);
    timer_start = std::chrono::steady_clock::now();
    auto next_oracle = EncodeOracle(ctx, next_domain, next_polynomial);
    encode_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    timer_start = std::chrono::steady_clock::now();
    auto next_tree = build_oracle_tree(pp.hash_profile, ctx, next_oracle);
    merkle_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    round.g_root = next_tree.root();
    timer_start = std::chrono::steady_clock::now();
    transcript.absorb_labeled_bytes(
        indexed_label(kTranscriptLabelGRoot,
                      static_cast<std::uint64_t>(layer)),
        round.g_root);
    transcript_ms +=
        ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

    const std::uint64_t shift_domain_size =
        current_domain.size() / pow3_checked(width);
    timer_start = std::chrono::steady_clock::now();
    const auto shift_positions = derive_unique_positions(
        transcript,
        indexed_label(kTranscriptLabelShift,
                      static_cast<std::uint64_t>(layer)),
        shift_domain_size, pp.shift_repetitions[layer]);
    transcript_ms +=
        ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    std::vector<stir_whir_gr::algebra::GRElem> folded_for_queries;
    const bool dense_shift_queries =
        shift_positions.size() >=
        static_cast<std::size_t>((shift_domain_size + 1U) / 2U);
    if (dense_shift_queries) {
      timer_start = std::chrono::steady_clock::now();
      folded_for_queries =
          repeated_ternary_fold_table(current_domain, current_oracle, alphas);
      fold_ms +=
          ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    }
    std::vector<std::uint64_t> parent_indices;
    parent_indices.reserve(shift_positions.size() *
                           static_cast<std::size_t>(pow3_checked(width)));
    const Domain shift_domain = current_domain.pow_map(pow3_checked(width));
    std::vector<std::vector<stir_whir_gr::algebra::GRElem>> shift_points;
    std::vector<stir_whir_gr::algebra::GRElem> shift_values;
    shift_points.reserve(shift_positions.size());
    shift_values.reserve(shift_positions.size());
    if (dense_shift_queries) {
      for (const auto shift_index : shift_positions) {
        const auto indices = virtual_fold_query_indices(current_domain.size(),
                                                       width, shift_index);
        parent_indices.insert(parent_indices.end(), indices.begin(),
                              indices.end());
        shift_values.push_back(
            folded_for_queries[static_cast<std::size_t>(shift_index)]);
        shift_points.push_back(
            pow_m(ctx, shift_domain.element(shift_index),
                  next_polynomial.variable_count()));
      }
    } else {
      timer_start = std::chrono::steady_clock::now();
      ctx.with_ntl_context([&] {
        for (const auto shift_index : shift_positions) {
          const auto indices = virtual_fold_query_indices(current_domain.size(),
                                                         width, shift_index);
          parent_indices.insert(parent_indices.end(), indices.begin(),
                                indices.end());
          shift_values.push_back(EvaluateVirtualFoldQueryFromOracle(
              current_domain, current_oracle, indices, alphas));
          shift_points.push_back(
              pow_m(ctx, shift_domain.element(shift_index),
                    next_polynomial.variable_count()));
        }
        return 0;
      });
      fold_ms +=
          ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    }
    timer_start = std::chrono::steady_clock::now();
    round.virtual_fold_openings =
        current_tree->open(SortedUnique(std::move(parent_indices)));
    query_open_ms +=
        ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

    timer_start = std::chrono::steady_clock::now();
    const auto gamma = transcript.challenge_teichmuller(
        ctx, indexed_label(kTranscriptLabelGamma,
                           static_cast<std::uint64_t>(layer)));
    transcript_ms +=
        ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
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
    current_tree_storage = std::move(next_tree);
    current_tree = &(*current_tree_storage);
    constraint = std::move(next_constraint);
  }

  opening.proof.final_constant = ctx.with_ntl_context([&] {
    const std::vector<stir_whir_gr::algebra::GRElem> empty_point;
    return current_polynomial.evaluate(ctx, empty_point);
  });
  timer_start = std::chrono::steady_clock::now();
  transcript.absorb_labeled_ring(kTranscriptLabelFinalConstant, ctx,
                                 opening.proof.final_constant);
  const auto final_positions = derive_unique_positions(
      transcript, kTranscriptLabelFinalQuery, current_domain.size(),
      pp.final_repetitions);
  transcript_ms +=
      ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
  timer_start = std::chrono::steady_clock::now();
  opening.proof.final_openings = current_tree->open(final_positions);
  query_open_ms +=
      ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

  opening.proof.stats.prover_rounds =
      static_cast<std::uint64_t>(opening.proof.rounds.size());
  opening.proof.stats.serialized_bytes = serialized_message_bytes(ctx, opening);
  opening.proof.stats.prover_encode_ms = encode_ms;
  opening.proof.stats.prover_merkle_ms = merkle_ms;
  opening.proof.stats.prover_transcript_ms = transcript_ms;
  opening.proof.stats.prover_fold_ms = fold_ms;
  opening.proof.stats.prover_interpolate_ms = interpolate_ms;
  opening.proof.stats.prover_query_open_ms = query_open_ms;
  opening.proof.stats.prover_total_ms =
      ElapsedMilliseconds(open_start, std::chrono::steady_clock::now());
  opening.proof.stats.prove_query_phase_ms =
      opening.proof.stats.prover_total_ms;
  return opening;
}

}  // namespace stir_whir_gr::whir
