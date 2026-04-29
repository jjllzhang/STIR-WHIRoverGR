#include "whir/verifier.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

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

std::vector<std::uint64_t> SortedUnique(std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

bool SameQueries(std::vector<std::uint64_t> expected,
                 const swgr::crypto::MerkleProof& proof) {
  return SortedUnique(std::move(expected)) == proof.queried_indices;
}

std::vector<std::vector<std::uint8_t>> PayloadsForIndices(
    const swgr::crypto::MerkleProof& proof,
    const std::vector<std::uint64_t>& indices) {
  std::vector<std::vector<std::uint8_t>> payloads;
  payloads.reserve(indices.size());
  for (const auto index : indices) {
    const auto it =
        std::find(proof.queried_indices.begin(), proof.queried_indices.end(),
                  index);
    if (it == proof.queried_indices.end()) {
      throw std::invalid_argument("missing WHIR Merkle payload for query");
    }
    const std::size_t offset =
        static_cast<std::size_t>(it - proof.queried_indices.begin());
    payloads.push_back(proof.leaf_payloads[offset]);
  }
  return payloads;
}

}  // namespace

WhirVerifier::WhirVerifier(WhirParameters params) : params_(std::move(params)) {}

bool WhirVerifier::verify(const WhirCommitment& commitment,
                          std::span<const swgr::algebra::GRElem> point,
                          const WhirOpening& opening,
                          swgr::ProofStatistics* stats) const {
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

  swgr::crypto::Transcript transcript(pp.hash_profile);
  absorb_opening_preamble(transcript, commitment, point, opening.value);

  WhirConstraint constraint(pp.ternary_grid);
  constraint.add_shift_term(ctx.one(),
                            std::vector<swgr::algebra::GRElem>(point.begin(),
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

    std::vector<swgr::algebra::GRElem> alphas;
    alphas.reserve(static_cast<std::size_t>(width));
    for (std::uint64_t j = 0; j < width; ++j) {
      const auto& h = round.sumcheck_polynomials[static_cast<std::size_t>(j)];
      absorb_sumcheck_polynomial(transcript, ctx, h);
      if (!check_sumcheck_identity(ctx, pp.ternary_grid, h, sigma,
                                   pp.degree_bounds[layer])) {
        return false;
      }
      const auto alpha = transcript.challenge_teichmuller(
          ctx, indexed_label(kTranscriptLabelAlpha,
                             static_cast<std::uint64_t>(layer), j));
      sigma = sumcheck_next_sigma(ctx, h, alpha);
      alphas.push_back(alpha);
    }

    transcript.absorb_bytes(round.g_root);
    const std::uint64_t fold_width = pow3_checked(width);
    const std::uint64_t shift_domain_size = current_domain.size() / fold_width;
    const auto shift_positions = derive_unique_positions(
        transcript,
        indexed_label(kTranscriptLabelShift,
                      static_cast<std::uint64_t>(layer)),
        shift_domain_size, pp.shift_repetitions[layer]);

    std::vector<std::uint64_t> expected_parent_indices;
    expected_parent_indices.reserve(shift_positions.size() *
                                    static_cast<std::size_t>(fold_width));
    for (const auto shift_index : shift_positions) {
      const auto indices =
          virtual_fold_query_indices(current_domain.size(), width, shift_index);
      expected_parent_indices.insert(expected_parent_indices.end(),
                                     indices.begin(), indices.end());
    }
    if (!SameQueries(expected_parent_indices, round.virtual_fold_openings) ||
        !swgr::crypto::MerkleTree::verify(
            pp.hash_profile, static_cast<std::size_t>(current_domain.size()),
            current_root, round.virtual_fold_openings)) {
      return false;
    }

    const auto gamma = transcript.challenge_teichmuller(
        ctx, indexed_label(kTranscriptLabelGamma,
                           static_cast<std::uint64_t>(layer)));
    auto next_constraint = constraint.restrict_prefix(ctx, alphas);
    const Domain shift_domain = current_domain.pow_map(fold_width);
    const std::uint64_t next_variable_count = live_variables - width;
    try {
      ctx.with_ntl_context([&] {
        auto gamma_power = gamma;
        for (const auto shift_index : shift_positions) {
          const auto indices = virtual_fold_query_indices(
              current_domain.size(), width, shift_index);
          const auto payloads =
              PayloadsForIndices(round.virtual_fold_openings, indices);
          const auto folded_value =
              evaluate_virtual_fold_query_from_leaf_payloads(
                  current_domain, width, shift_index, payloads, alphas);
          const auto shift_point =
              pow_m(ctx, shift_domain.element(shift_index), next_variable_count);
          next_constraint.add_shift_term(gamma_power, shift_point);
          sigma += gamma_power * folded_value;
          gamma_power *= gamma;
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

  const auto final_ok = ctx.with_ntl_context([&] {
    const std::vector<swgr::algebra::GRElem> empty_point;
    return constraint.evaluate_W(ctx, opening.proof.final_constant,
                                 empty_point) == sigma;
  });
  if (!final_ok) {
    return false;
  }

  transcript.absorb_ring(ctx, opening.proof.final_constant);
  const auto final_positions = derive_unique_positions(
      transcript, kTranscriptLabelFinalQuery, current_domain.size(),
      pp.final_repetitions);
  if (!SameQueries(final_positions, opening.proof.final_openings) ||
      !swgr::crypto::MerkleTree::verify(
          pp.hash_profile, static_cast<std::size_t>(current_domain.size()),
          current_root, opening.proof.final_openings)) {
    return false;
  }
  for (const auto& payload : opening.proof.final_openings.leaf_payloads) {
    if (!(ctx.deserialize(payload) == opening.proof.final_constant)) {
      return false;
    }
  }

  if (stats != nullptr) {
    stats->verifier_total_ms =
        ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
    stats->serialized_bytes = serialized_message_bytes(ctx, opening);
    stats->prover_rounds =
        static_cast<std::uint64_t>(opening.proof.rounds.size());
  }
  return true;
}

bool WhirVerifier::verify(const WhirProof& proof) const {
  (void)params_;
  (void)proof;
  throw_unimplemented("whir::WhirVerifier::verify");
}

}  // namespace swgr::whir
