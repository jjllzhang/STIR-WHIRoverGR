#include "fri/verifier.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "GaloisRing/Inverse.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"

namespace swgr::fri {
namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

std::string RoundLabel(const char* prefix, std::size_t round_index) {
  return std::string(prefix) + ":" + std::to_string(round_index);
}

std::vector<std::uint64_t> UniqueSorted(
    const std::vector<std::uint64_t>& values) {
  std::vector<std::uint64_t> unique = values;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  return unique;
}

std::vector<std::uint64_t> ExpandFiberIndices(
    const std::vector<std::uint64_t>& child_queries,
    std::uint64_t child_domain_size, std::uint64_t fold_factor) {
  std::vector<std::uint64_t> indices;
  indices.reserve(child_queries.size() *
                  static_cast<std::size_t>(fold_factor));
  for (const auto child_index : child_queries) {
    for (std::uint64_t offset = 0; offset < fold_factor; ++offset) {
      indices.push_back(child_index + offset * child_domain_size);
    }
  }
  return UniqueSorted(indices);
}

void AbsorbEvaluationClaim(swgr::crypto::Transcript& transcript,
                           const swgr::algebra::GRContext& ctx,
                           const swgr::algebra::GRElem& alpha,
                           const swgr::algebra::GRElem& value) {
  transcript.absorb_bytes(ctx.serialize(alpha));
  transcript.absorb_bytes(ctx.serialize(value));
}

const swgr::algebra::GRElem* LookupIndexedValue(
    const std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>& indexed,
    std::uint64_t index) {
  const auto it = std::lower_bound(
      indexed.begin(), indexed.end(), index,
      [](const auto& entry, std::uint64_t candidate) {
        return entry.first < candidate;
      });
  if (it == indexed.end() || it->first != index) {
    return nullptr;
  }
  return &it->second;
}

bool DecodeSingletonValues(
    const swgr::algebra::GRContext& ctx, const swgr::crypto::MerkleProof& proof,
    std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>* indexed) {
  indexed->clear();
  if (proof.leaf_payloads.size() != proof.queried_indices.size()) {
    return false;
  }

  indexed->reserve(proof.queried_indices.size());
  for (std::size_t i = 0; i < proof.queried_indices.size(); ++i) {
    const auto values = deserialize_oracle_bundle(ctx, proof.leaf_payloads[i]);
    if (values.size() != 1U) {
      return false;
    }
    indexed->push_back({proof.queried_indices[i], values.front()});
  }
  std::sort(indexed->begin(), indexed->end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.first < rhs.first;
            });
  return true;
}

swgr::algebra::GRElem BasisWeight(
    const std::vector<swgr::algebra::GRElem>& fiber_points,
    std::size_t missing_index, const swgr::algebra::GRElem& folding_alpha,
    const swgr::algebra::GRContext& ctx) {
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> basis(fiber_points.size(), ctx.zero());
    basis[missing_index] = ctx.one();
    return swgr::poly_utils::fold_eval_k(fiber_points, basis, folding_alpha);
  });
}

bool VerifyExplicitFiber(
    const Domain& parent_domain, std::uint64_t child_index,
    std::uint64_t child_domain_size, std::uint64_t fold_factor,
    const std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>& indexed_parent,
    const swgr::algebra::GRElem& folding_alpha,
    const swgr::algebra::GRElem& child_value) {
  const auto& ctx = parent_domain.context();
  try {
    return ctx.with_ntl_context([&]() -> bool {
      std::vector<swgr::algebra::GRElem> fiber_points;
      std::vector<swgr::algebra::GRElem> fiber_values;
      fiber_points.reserve(static_cast<std::size_t>(fold_factor));
      fiber_values.reserve(static_cast<std::size_t>(fold_factor));
      for (std::uint64_t offset = 0; offset < fold_factor; ++offset) {
        const std::uint64_t parent_index = child_index + offset * child_domain_size;
        const auto* value_ptr = LookupIndexedValue(indexed_parent, parent_index);
        if (value_ptr == nullptr) {
          return false;
        }
        fiber_points.push_back(parent_domain.element(parent_index));
        fiber_values.push_back(*value_ptr);
      }
      return swgr::poly_utils::fold_eval_k(fiber_points, fiber_values,
                                           folding_alpha) == child_value;
    });
  } catch (...) {
    return false;
  }
}

bool VerifyVirtualFirstRoundFiber(
    const FriParameters& params, const FriCommitment& commitment,
    const std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>& indexed_f,
    std::uint64_t child_index, std::uint64_t child_domain_size,
    const swgr::algebra::GRElem& alpha, const swgr::algebra::GRElem& value,
    const swgr::algebra::GRElem& folding_alpha,
    const swgr::algebra::GRElem& child_value) {
  const auto& domain = commitment.domain;
  const auto& ctx = domain.context();
  try {
    return ctx.with_ntl_context([&]() -> bool {
      std::vector<swgr::algebra::GRElem> fiber_points;
      std::vector<swgr::algebra::GRElem> derived_values;
      fiber_points.reserve(static_cast<std::size_t>(params.fold_factor));
      derived_values.reserve(static_cast<std::size_t>(params.fold_factor));

      bool saw_alpha = false;
      std::size_t alpha_offset = 0;
      for (std::uint64_t offset = 0; offset < params.fold_factor; ++offset) {
        const std::uint64_t parent_index =
            child_index + offset * child_domain_size;
        const auto* f_value = LookupIndexedValue(indexed_f, parent_index);
        if (f_value == nullptr) {
          return false;
        }

        const auto point = domain.element(parent_index);
        fiber_points.push_back(point);
        if (point == alpha) {
          if (*f_value != value) {
            return false;
          }
          saw_alpha = true;
          alpha_offset = static_cast<std::size_t>(offset);
          derived_values.push_back(ctx.zero());
          continue;
        }

        const auto denominator = point - alpha;
        if (!ctx.is_unit(denominator)) {
          return false;
        }
        const auto denominator_inverse = Inv(
            denominator, static_cast<long>(ctx.config().r));
        if (denominator_inverse == 0) {
          return false;
        }
        derived_values.push_back((*f_value - value) * denominator_inverse);
      }

      if (!saw_alpha) {
        return swgr::poly_utils::fold_eval_k(fiber_points, derived_values,
                                             folding_alpha) == child_value;
      }

      const auto known_fold = swgr::poly_utils::fold_eval_k(
          fiber_points, derived_values, folding_alpha);
      const auto alpha_weight =
          BasisWeight(fiber_points, alpha_offset, folding_alpha, ctx);
      const auto residual = child_value - known_fold;
      if (alpha_weight == 0) {
        return residual == 0;
      }
      if (ctx.is_unit(alpha_weight)) {
        // `g_0(alpha)` is virtual. Once the folding weight is a unit, the opened
        // child value determines a unique missing parent slot, so no extra
        // first-round oracle opening is needed here.
        return true;
      }
      return false;
    });
  } catch (...) {
    return false;
  }
}

bool VerifyZeroFoldVirtualQuotient(
    const FriCommitment& commitment,
    const std::vector<swgr::algebra::GRElem>& committed_oracle,
    const swgr::algebra::GRElem& alpha, const swgr::algebra::GRElem& value,
    std::uint64_t reduced_degree_bound) {
  const auto& domain = commitment.domain;
  const auto& ctx = domain.context();
  try {
    return ctx.with_ntl_context([&]() -> bool {
      std::vector<swgr::algebra::GRElem> quotient_points;
      std::vector<swgr::algebra::GRElem> quotient_values;
      quotient_points.reserve(domain.size());
      quotient_values.reserve(domain.size());

      bool saw_alpha = false;
      for (std::uint64_t index = 0; index < domain.size(); ++index) {
        const auto point = domain.element(index);
        const auto& f_value = committed_oracle[static_cast<std::size_t>(index)];
        if (point == alpha) {
          if (f_value != value) {
            return false;
          }
          saw_alpha = true;
          continue;
        }

        const auto denominator = point - alpha;
        if (!ctx.is_unit(denominator)) {
          return false;
        }
        const auto denominator_inverse = Inv(
            denominator, static_cast<long>(ctx.config().r));
        if (denominator_inverse == 0) {
          return false;
        }
        quotient_points.push_back(point);
        quotient_values.push_back((f_value - value) * denominator_inverse);
      }

      if (quotient_points.empty()) {
        return reduced_degree_bound == 0;
      }

      const auto quotient_polynomial = swgr::poly_utils::interpolate_for_gr_wrapper(
          ctx, quotient_points, quotient_values);
      if (quotient_polynomial.degree() > reduced_degree_bound) {
        return false;
      }

      if (!saw_alpha) {
        return true;
      }

      for (std::size_t i = 0; i < quotient_points.size(); ++i) {
        if (quotient_polynomial.evaluate(ctx, quotient_points[i]) !=
            quotient_values[i]) {
          return false;
        }
      }
      return true;
    });
  } catch (...) {
    return false;
  }
}

}  // namespace

FriVerifier::FriVerifier(FriParameters params) : params_(std::move(params)) {}

bool FriVerifier::verify(const FriCommitment& commitment,
                         const swgr::algebra::GRElem& alpha,
                         const swgr::algebra::GRElem& value,
                         const FriOpening& opening,
                         swgr::ProofStatistics* stats) const {
  swgr::ProofStatistics local_stats;
  const auto verify_start = std::chrono::steady_clock::now();
  try {
    if (!validate(params_, commitment) ||
        !validate(commitment, FriOpeningClaim{.alpha = alpha, .value = value}) ||
        opening.claim.alpha != alpha || opening.claim.value != value) {
      return false;
    }

    const FriInstance reduced_instance = opening_instance(commitment);
    const auto total_rounds =
        folding_round_count(reduced_instance, params_.fold_factor, params_.stop_degree);
    const auto& ctx = commitment.domain.context();

    if (total_rounds == 0) {
      if (!opening.proof.oracle_roots.empty() || !opening.proof.rounds.empty() ||
          opening.proof.revealed_committed_oracle.size() !=
              static_cast<std::size_t>(commitment.domain.size()) ||
          !opening.proof.final_oracle.empty()) {
        return false;
      }

      const auto merkle_start = std::chrono::steady_clock::now();
      const auto committed_tree = build_oracle_tree(params_.hash_profile, ctx,
                                                    opening.proof.revealed_committed_oracle,
                                                    1);
      if (committed_tree.root() != commitment.oracle_root) {
        return false;
      }
      local_stats.verifier_merkle_ms +=
          ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());

      const auto algebra_start = std::chrono::steady_clock::now();
      const bool algebra_ok = VerifyZeroFoldVirtualQuotient(
          commitment, opening.proof.revealed_committed_oracle, alpha, value,
          reduced_instance.claimed_degree);
      if (!algebra_ok) {
        return false;
      }
      local_stats.verifier_algebra_ms +=
          ElapsedMilliseconds(algebra_start, std::chrono::steady_clock::now());
      local_stats.verifier_total_ms =
          ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
      if (stats != nullptr) {
        *stats = local_stats;
      }
      return true;
    }

    if (opening.proof.oracle_roots.size() != total_rounds ||
        opening.proof.rounds.size() != total_rounds ||
        !opening.proof.revealed_committed_oracle.empty() ||
        opening.proof.final_oracle.empty()) {
      return false;
    }

    std::vector<swgr::algebra::GRElem> folding_betas;
    folding_betas.reserve(total_rounds);
    std::vector<Domain> oracle_domains;
    oracle_domains.reserve(total_rounds);
    swgr::crypto::Transcript transcript(params_.hash_profile);
    transcript.absorb_bytes(commitment.oracle_root);
    AbsorbEvaluationClaim(transcript, ctx, alpha, value);

    Domain current_domain = reduced_instance.domain;
    for (std::size_t round_index = 0; round_index < total_rounds; ++round_index) {
      const auto transcript_start = std::chrono::steady_clock::now();
      const auto beta = derive_fri_folding_challenge(
          transcript, ctx, RoundLabel("fri.fold_alpha", round_index));
      folding_betas.push_back(beta);
      current_domain = current_domain.pow_map(params_.fold_factor);
      oracle_domains.push_back(current_domain);
      transcript.absorb_bytes(opening.proof.oracle_roots[round_index]);
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_start, std::chrono::steady_clock::now());
    }

    const auto merkle_start = std::chrono::steady_clock::now();
    const auto final_tree = build_oracle_tree(params_.hash_profile, ctx,
                                              opening.proof.final_oracle, 1);
    if (final_tree.root() != opening.proof.oracle_roots.back()) {
      return false;
    }
    local_stats.verifier_merkle_ms +=
        ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());

    const auto algebra_start = std::chrono::steady_clock::now();
    std::uint64_t final_degree_bound = reduced_instance.claimed_degree;
    for (std::size_t round_index = 0; round_index < total_rounds; ++round_index) {
      final_degree_bound /= params_.fold_factor;
    }
    const bool final_degree_ok = ctx.with_ntl_context([&]() -> bool {
      const auto final_polynomial = swgr::poly_utils::rs_interpolate(
          oracle_domains.back(), opening.proof.final_oracle);
      return final_polynomial.degree() <= final_degree_bound;
    });
    if (!final_degree_ok) {
      return false;
    }
    local_stats.verifier_algebra_ms +=
        ElapsedMilliseconds(algebra_start, std::chrono::steady_clock::now());

    const auto query_rounds = resolve_query_rounds_metadata(params_, reduced_instance);
    Domain parent_domain = reduced_instance.domain;
    for (std::size_t round_index = 0; round_index < total_rounds; ++round_index) {
      const Domain& child_domain = oracle_domains[round_index];
      const auto query_start = std::chrono::steady_clock::now();
      const auto round_queries = derive_query_positions(
          transcript, RoundLabel("fri.query", round_index), child_domain.size(),
          query_rounds[round_index].fresh_query_count);
      local_stats.verifier_transcript_ms +=
          ElapsedMilliseconds(query_start, std::chrono::steady_clock::now());
      const auto expected_parent_indices =
          ExpandFiberIndices(round_queries, child_domain.size(), params_.fold_factor);
      const auto parent_root =
          round_index == 0 ? commitment.oracle_root
                           : opening.proof.oracle_roots[round_index - 1];
      const auto& round = opening.proof.rounds[round_index];

      const auto verify_parent_start = std::chrono::steady_clock::now();
      if (round.parent_oracle_proof.queried_indices != expected_parent_indices ||
          !swgr::crypto::MerkleTree::verify(params_.hash_profile, parent_domain.size(),
                                           parent_root, round.parent_oracle_proof)) {
        return false;
      }
      local_stats.verifier_merkle_ms += ElapsedMilliseconds(
          verify_parent_start, std::chrono::steady_clock::now());

      std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>> indexed_parent;
      if (!DecodeSingletonValues(ctx, round.parent_oracle_proof, &indexed_parent)) {
        return false;
      }

      std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>> indexed_child;
      if (round_index + 1U < total_rounds) {
        const auto expected_child_indices = UniqueSorted(round_queries);
        const auto verify_child_start = std::chrono::steady_clock::now();
        if (round.child_oracle_proof.queried_indices != expected_child_indices ||
            !swgr::crypto::MerkleTree::verify(
                params_.hash_profile, child_domain.size(),
                opening.proof.oracle_roots[round_index],
                round.child_oracle_proof)) {
          return false;
        }
        local_stats.verifier_merkle_ms += ElapsedMilliseconds(
            verify_child_start, std::chrono::steady_clock::now());
        if (!DecodeSingletonValues(ctx, round.child_oracle_proof, &indexed_child)) {
          return false;
        }
      } else if (!round.child_oracle_proof.queried_indices.empty() ||
                 !round.child_oracle_proof.leaf_payloads.empty() ||
                 !round.child_oracle_proof.sibling_hashes.empty()) {
        return false;
      }

      const auto round_algebra_start = std::chrono::steady_clock::now();
      for (const auto child_index : round_queries) {
        const swgr::algebra::GRElem* child_value_ptr = nullptr;
        if (round_index + 1U < total_rounds) {
          child_value_ptr = LookupIndexedValue(indexed_child, child_index);
        } else {
          child_value_ptr =
              &opening.proof.final_oracle[static_cast<std::size_t>(child_index)];
        }
        if (child_value_ptr == nullptr) {
          return false;
        }

        const bool round_ok =
            round_index == 0
                ? VerifyVirtualFirstRoundFiber(
                      params_, commitment, indexed_parent, child_index,
                      child_domain.size(), alpha, value,
                      folding_betas[round_index], *child_value_ptr)
                : VerifyExplicitFiber(parent_domain, child_index,
                                     child_domain.size(), params_.fold_factor,
                                     indexed_parent, folding_betas[round_index],
                                     *child_value_ptr);
        if (!round_ok) {
          return false;
        }
      }
      local_stats.verifier_algebra_ms += ElapsedMilliseconds(
          round_algebra_start, std::chrono::steady_clock::now());
      parent_domain = child_domain;
    }

    local_stats.verifier_hashes = opening.proof.stats.verifier_hashes;
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

}  // namespace swgr::fri
