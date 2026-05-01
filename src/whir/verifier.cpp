#include "whir/verifier.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "whir/constraint.hpp"
#include "whir/folding.hpp"

namespace stir_whir_gr::whir {
namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

std::vector<std::uint64_t> SortedUnique(std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

bool SameQueries(std::vector<std::uint64_t> expected,
                 const stir_whir_gr::crypto::MerkleProof& proof) {
  return SortedUnique(std::move(expected)) == proof.queried_indices;
}

using PayloadRef = const std::vector<std::uint8_t>*;

std::vector<PayloadRef> PayloadRefsForIndices(
    const stir_whir_gr::crypto::MerkleProof& proof,
    std::span<const std::uint64_t> indices) {
  std::vector<PayloadRef> payloads;
  payloads.reserve(indices.size());
  for (const auto index : indices) {
    const auto it = std::lower_bound(proof.queried_indices.begin(),
                                     proof.queried_indices.end(), index);
    if (it == proof.queried_indices.end() || *it != index) {
      throw std::invalid_argument("missing WHIR Merkle payload for query");
    }
    const std::size_t offset =
        static_cast<std::size_t>(it - proof.queried_indices.begin());
    if (offset >= proof.leaf_payloads.size()) {
      throw std::invalid_argument("missing WHIR Merkle payload for query");
    }
    payloads.push_back(&proof.leaf_payloads[offset]);
  }
  return payloads;
}

stir_whir_gr::algebra::GRElem EvaluateVirtualFoldQueryFromPayloadRefs(
    const Domain& domain, std::span<const std::uint64_t> parent_indices,
    std::span<const PayloadRef> payloads,
    std::span<const stir_whir_gr::algebra::GRElem> alphas) {
  if (payloads.size() != parent_indices.size()) {
    throw std::invalid_argument(
        "WHIR virtual fold query requires one payload per parent index");
  }

  std::vector<stir_whir_gr::algebra::GRElem> points;
  std::vector<stir_whir_gr::algebra::GRElem> values;
  points.reserve(parent_indices.size());
  values.reserve(parent_indices.size());
  for (std::size_t i = 0; i < parent_indices.size(); ++i) {
    points.push_back(domain.element(parent_indices[i]));
    values.push_back(domain.context().deserialize(*payloads[i]));
  }

  return evaluate_repeated_ternary_fold_from_values(points, values, alphas);
}

}  // namespace

WhirVerifier::WhirVerifier(WhirParameters params) : params_(std::move(params)) {}

bool WhirVerifier::verify(const WhirCommitment& commitment,
                          std::span<const stir_whir_gr::algebra::GRElem> point,
                          const WhirOpening& opening,
                          stir_whir_gr::ProofStatistics* stats) const {
  const auto verify_start = std::chrono::steady_clock::now();
  if (!validate(params_, commitment) ||
      point.size() !=
          static_cast<std::size_t>(commitment.public_params.variable_count) ||
      !proof_shape_valid(opening.proof)) {
    return false;
  }

  const auto& pp = commitment.public_params;
  const auto& ctx = *pp.ctx;
  if (opening.proof.rounds.size() != pp.layer_widths.size()) {
    return false;
  }

  double merkle_ms = 0.0;
  double transcript_ms = 0.0;
  double query_ms = 0.0;
  double algebra_ms = 0.0;
  auto timer_start = std::chrono::steady_clock::now();

  stir_whir_gr::crypto::Transcript transcript(pp.hash_profile);
  absorb_opening_preamble(transcript, commitment, point, opening.value);
  transcript_ms +=
      ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

  WhirConstraint constraint(pp.ternary_grid);
  constraint.add_shift_term(ctx.one(),
                            std::vector<stir_whir_gr::algebra::GRElem>(point.begin(),
                                                                point.end()));
  auto sigma = opening.value;
  auto current_domain = pp.initial_domain;
  auto current_root = commitment.oracle_root;
  std::uint64_t live_variables = pp.variable_count;

  for (std::size_t layer = 0; layer < pp.layer_widths.size(); ++layer) {
    const auto& round = opening.proof.rounds[layer];
    const std::uint64_t width = pp.layer_widths[layer];
    if (round.sumcheck_polynomials.size() != static_cast<std::size_t>(width) ||
        width > live_variables || round.g_root.empty()) {
      return false;
    }

    std::vector<stir_whir_gr::algebra::GRElem> alphas;
    alphas.reserve(static_cast<std::size_t>(width));
    for (std::uint64_t j = 0; j < width; ++j) {
      const auto& h = round.sumcheck_polynomials[static_cast<std::size_t>(j)];
      timer_start = std::chrono::steady_clock::now();
      absorb_sumcheck_polynomial(
          transcript, ctx,
          indexed_label(kTranscriptLabelSumcheckPolynomial,
                        static_cast<std::uint64_t>(layer), j),
          h);
      const auto alpha = transcript.challenge_teichmuller(
          ctx, indexed_label(kTranscriptLabelAlpha,
                             static_cast<std::uint64_t>(layer), j));
      transcript_ms +=
          ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

      timer_start = std::chrono::steady_clock::now();
      const bool sumcheck_ok =
          check_sumcheck_identity(ctx, pp.ternary_grid, h, sigma,
                                  pp.degree_bounds[layer]);
      algebra_ms +=
          ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
      if (!sumcheck_ok) {
        return false;
      }
      timer_start = std::chrono::steady_clock::now();
      sigma = sumcheck_next_sigma(ctx, h, alpha);
      algebra_ms +=
          ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
      alphas.push_back(alpha);
    }

    timer_start = std::chrono::steady_clock::now();
    transcript.absorb_labeled_bytes(
        indexed_label(kTranscriptLabelGRoot,
                      static_cast<std::uint64_t>(layer)),
        round.g_root);
    transcript_ms +=
        ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    const std::uint64_t fold_width = pow3_checked(width);
    const std::uint64_t shift_domain_size = current_domain.size() / fold_width;
    timer_start = std::chrono::steady_clock::now();
    const auto shift_positions = derive_unique_positions(
        transcript,
        indexed_label(kTranscriptLabelShift,
                      static_cast<std::uint64_t>(layer)),
        shift_domain_size, pp.shift_repetitions[layer]);
    transcript_ms +=
        ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

    timer_start = std::chrono::steady_clock::now();
    std::vector<std::uint64_t> expected_parent_indices;
    expected_parent_indices.reserve(shift_positions.size() *
                                    static_cast<std::size_t>(fold_width));
    std::vector<std::vector<std::uint64_t>> parent_indices_by_shift;
    parent_indices_by_shift.reserve(shift_positions.size());
    for (const auto shift_index : shift_positions) {
      auto indices =
          virtual_fold_query_indices(current_domain.size(), width, shift_index);
      expected_parent_indices.insert(expected_parent_indices.end(),
                                     indices.begin(), indices.end());
      parent_indices_by_shift.push_back(std::move(indices));
    }
    const bool same_queries =
        SameQueries(expected_parent_indices, round.virtual_fold_openings);
    query_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    if (!same_queries) {
      return false;
    }
    timer_start = std::chrono::steady_clock::now();
    const bool merkle_ok = stir_whir_gr::crypto::MerkleTree::verify(
        pp.hash_profile, static_cast<std::size_t>(current_domain.size()),
        current_root, round.virtual_fold_openings);
    merkle_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    if (!merkle_ok) {
      return false;
    }

    timer_start = std::chrono::steady_clock::now();
    const auto gamma = transcript.challenge_teichmuller(
        ctx, indexed_label(kTranscriptLabelGamma,
                           static_cast<std::uint64_t>(layer)));
    transcript_ms +=
        ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    timer_start = std::chrono::steady_clock::now();
    auto next_constraint = constraint.restrict_prefix(ctx, alphas);
    algebra_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
    const Domain shift_domain = current_domain.pow_map(fold_width);
    const std::uint64_t next_variable_count = live_variables - width;
    try {
      ctx.with_ntl_context([&] {
        auto gamma_power = gamma;
        for (std::size_t i = 0; i < shift_positions.size(); ++i) {
          const auto shift_index = shift_positions[i];
          const auto& indices = parent_indices_by_shift[i];
          auto inner_start = std::chrono::steady_clock::now();
          const auto payloads = PayloadRefsForIndices(round.virtual_fold_openings,
                                                      indices);
          const auto folded_value = EvaluateVirtualFoldQueryFromPayloadRefs(
              current_domain, indices, payloads, alphas);
          query_ms +=
              ElapsedMilliseconds(inner_start, std::chrono::steady_clock::now());

          inner_start = std::chrono::steady_clock::now();
          const auto shift_point =
              pow_m(ctx, shift_domain.element(shift_index), next_variable_count);
          next_constraint.add_shift_term(gamma_power, shift_point);
          sigma += gamma_power * folded_value;
          gamma_power *= gamma;
          algebra_ms +=
              ElapsedMilliseconds(inner_start, std::chrono::steady_clock::now());
        }
        return 0;
      });
    } catch (const std::exception&) {
      return false;
    }

    live_variables = next_variable_count;
    constraint = std::move(next_constraint);
    current_root = round.g_root;
    current_domain = current_domain.pow_map(3U);
  }

  timer_start = std::chrono::steady_clock::now();
  const auto final_ok = ctx.with_ntl_context([&] {
    const std::vector<stir_whir_gr::algebra::GRElem> empty_point;
    return constraint.evaluate_W(ctx, opening.proof.final_constant,
                                 empty_point) == sigma;
  });
  algebra_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
  if (!final_ok) {
    return false;
  }

  timer_start = std::chrono::steady_clock::now();
  transcript.absorb_labeled_ring(kTranscriptLabelFinalConstant, ctx,
                                 opening.proof.final_constant);
  const auto final_positions = derive_unique_positions(
      transcript, kTranscriptLabelFinalQuery, current_domain.size(),
      pp.final_repetitions);
  transcript_ms +=
      ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

  timer_start = std::chrono::steady_clock::now();
  const bool same_final_queries =
      SameQueries(final_positions, opening.proof.final_openings);
  query_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
  if (!same_final_queries) {
    return false;
  }
  timer_start = std::chrono::steady_clock::now();
  const bool final_merkle_ok = stir_whir_gr::crypto::MerkleTree::verify(
      pp.hash_profile, static_cast<std::size_t>(current_domain.size()),
      current_root, opening.proof.final_openings);
  merkle_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());
  if (!final_merkle_ok) {
    return false;
  }
  timer_start = std::chrono::steady_clock::now();
  for (const auto& payload : opening.proof.final_openings.leaf_payloads) {
    if (!(ctx.deserialize(payload) == opening.proof.final_constant)) {
      return false;
    }
  }
  query_ms += ElapsedMilliseconds(timer_start, std::chrono::steady_clock::now());

  if (stats != nullptr) {
    stats->verifier_merkle_ms = merkle_ms;
    stats->verifier_transcript_ms = transcript_ms;
    stats->verifier_query_phase_ms = query_ms;
    stats->verifier_algebra_ms = algebra_ms;
    stats->verifier_total_ms =
        ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
    stats->serialized_bytes = serialized_message_bytes(ctx, opening);
    stats->prover_rounds =
        static_cast<std::uint64_t>(opening.proof.rounds.size());
  }
  return true;
}

}  // namespace stir_whir_gr::whir
