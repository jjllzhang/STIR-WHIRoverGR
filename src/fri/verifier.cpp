#include "fri/verifier.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "poly_utils/folding.hpp"

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

std::vector<std::uint64_t> CarryToBundleQueryChains(
    const std::vector<std::uint64_t>& carried_positions,
    std::uint64_t bundle_count) {
  if (bundle_count == 0) {
    return {};
  }
  std::vector<std::uint64_t> queries;
  queries.reserve(carried_positions.size());
  for (const auto position : carried_positions) {
    queries.push_back(position % bundle_count);
  }
  return queries;
}

std::vector<std::uint64_t> OpenedRoundQueries(
    const std::vector<std::uint64_t>& carried_positions,
    std::uint64_t bundle_count,
    const std::vector<std::uint64_t>& fresh_query_chains) {
  std::vector<std::uint64_t> opened =
      CarryToBundleQueryChains(carried_positions, bundle_count);
  opened.insert(opened.end(), fresh_query_chains.begin(),
                fresh_query_chains.end());
  return UniqueSorted(opened);
}

std::vector<std::uint64_t> NextCarriedQueryChains(
    const std::vector<std::uint64_t>& carried_positions,
    std::uint64_t bundle_count,
    const std::vector<std::uint64_t>& fresh_query_chains) {
  std::vector<std::uint64_t> next_positions =
      CarryToBundleQueryChains(carried_positions, bundle_count);
  next_positions.insert(next_positions.end(), fresh_query_chains.begin(),
                        fresh_query_chains.end());
  return next_positions;
}

std::vector<std::uint64_t> DeriveTerminalQueryChains(
    swgr::crypto::Transcript& transcript, const FriParameters& params,
    const FriInstance& instance, std::size_t round_index) {
  return derive_query_positions(
      transcript, RoundLabel("fri.final_query", round_index),
      instance.domain.size(), terminal_query_chain_count(params));
}

std::vector<std::uint64_t> ExpandBundleIndices(
    const std::vector<std::uint64_t>& bundle_queries,
    std::uint64_t bundle_count, std::uint64_t bundle_size) {
  std::vector<std::uint64_t> indices;
  indices.reserve(bundle_queries.size() *
                  static_cast<std::size_t>(bundle_size));
  for (const auto bundle_index : bundle_queries) {
    for (std::uint64_t offset = 0; offset < bundle_size; ++offset) {
      indices.push_back(bundle_index + offset * bundle_count);
    }
  }
  return UniqueSorted(indices);
}

void AccumulateVerifierStats(swgr::ProofStatistics& dst,
                             const swgr::ProofStatistics& src) {
  dst.verifier_merkle_ms += src.verifier_merkle_ms;
  dst.verifier_transcript_ms += src.verifier_transcript_ms;
  dst.verifier_query_phase_ms += src.verifier_query_phase_ms;
  dst.verifier_algebra_ms += src.verifier_algebra_ms;
}

bool DecodeBundles(const swgr::algebra::GRContext& ctx,
                   const swgr::crypto::MerkleProof& proof,
                   std::uint64_t bundle_size,
                   std::vector<std::vector<swgr::algebra::GRElem>>* bundles) {
  if (proof.queried_indices.size() != proof.leaf_payloads.size()) {
    return false;
  }
  bundles->clear();
  bundles->reserve(proof.leaf_payloads.size());
  try {
    for (const auto& payload : proof.leaf_payloads) {
      auto bundle = deserialize_oracle_bundle(ctx, payload);
      if (bundle.size() != static_cast<std::size_t>(bundle_size)) {
        return false;
      }
      bundles->push_back(std::move(bundle));
    }
  } catch (...) {
    return false;
  }
  return true;
}

bool VerifyCarriedValues(
    const std::vector<std::uint64_t>& carried_positions,
    const std::vector<swgr::algebra::GRElem>& carried_values,
    std::uint64_t bundle_count,
    const std::vector<std::uint64_t>& opened_queries,
    const std::vector<std::vector<swgr::algebra::GRElem>>& opened_bundles) {
  if (carried_positions.size() != carried_values.size() ||
      opened_queries.size() != opened_bundles.size()) {
    return false;
  }
  for (std::size_t i = 0; i < carried_positions.size(); ++i) {
    const std::uint64_t bundle_index =
        bundle_count == 0 ? 0 : (carried_positions[i] % bundle_count);
    const std::uint64_t checking_index =
        bundle_count == 0 ? 0 : (carried_positions[i] / bundle_count);
    const auto it = std::lower_bound(
        opened_queries.begin(), opened_queries.end(), bundle_index);
    if (it == opened_queries.end() || *it != bundle_index) {
      return false;
    }
    const std::size_t open_cursor =
        static_cast<std::size_t>(it - opened_queries.begin());
    if (checking_index >= opened_bundles[open_cursor].size() ||
        opened_bundles[open_cursor][static_cast<std::size_t>(checking_index)] !=
            carried_values[i]) {
      return false;
    }
  }
  return true;
}

bool EvaluateFinalPolynomial(
    const Domain& domain, const swgr::poly_utils::Polynomial& polynomial,
    const std::vector<std::uint64_t>& queries,
    const std::vector<std::vector<swgr::algebra::GRElem>>& bundles) {
  if (queries.size() != bundles.size()) {
    return false;
  }
  const auto& ctx = domain.context();
  for (std::size_t i = 0; i < queries.size(); ++i) {
    if (bundles[i].size() != 1U) {
      return false;
    }
    const auto expected = polynomial.evaluate(ctx, domain.element(queries[i]));
    if (expected != bundles[i].front()) {
      return false;
    }
  }
  return true;
}

std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>> IndexedSingletonValues(
    const std::vector<std::uint64_t>& indices,
    const std::vector<std::vector<swgr::algebra::GRElem>>& bundles) {
  std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>> indexed;
  indexed.reserve(indices.size());
  for (std::size_t i = 0; i < indices.size(); ++i) {
    indexed.emplace_back(indices[i], bundles[i].front());
  }
  return indexed;
}

const swgr::algebra::GRElem* LookupIndexedValue(
    const std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>& indexed,
    std::uint64_t index) {
  const auto it = std::lower_bound(
      indexed.begin(), indexed.end(), index,
      [](const auto& entry, std::uint64_t needle) { return entry.first < needle; });
  if (it == indexed.end() || it->first != index) {
    return nullptr;
  }
  return &it->second;
}

bool BuildVirtualIndexedValues(
    const Domain& domain,
    const std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>& indexed_f,
    const swgr::algebra::GRElem& alpha,
    const swgr::algebra::GRElem& value,
    std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>* indexed_g) {
  const auto& ctx = domain.context();
  try {
    return ctx.with_ntl_context([&] {
      std::vector<swgr::algebra::GRElem> denominators;
      denominators.reserve(indexed_f.size());
      for (const auto& [index, _] : indexed_f) {
        denominators.push_back(domain.element(index) - alpha);
      }
      const auto inverses = ctx.batch_inv(denominators);
      indexed_g->clear();
      indexed_g->reserve(indexed_f.size());
      for (std::size_t i = 0; i < indexed_f.size(); ++i) {
        indexed_g->emplace_back(indexed_f[i].first,
                                (indexed_f[i].second - value) * inverses[i]);
      }
      return true;
    });
  } catch (...) {
    return false;
  }
}

bool FoldVirtualQueries(
    const Domain& domain, std::uint64_t fold_factor,
    const std::vector<std::uint64_t>& base_queries,
    const std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>>& indexed_g,
    const swgr::algebra::GRElem& folding_alpha,
    std::vector<swgr::algebra::GRElem>* folded_values) {
  const std::uint64_t next_domain_size = domain.size() / fold_factor;
  folded_values->clear();
  folded_values->reserve(base_queries.size());
  try {
    return domain.context().with_ntl_context([&] {
      for (const auto base_index : base_queries) {
        std::vector<swgr::algebra::GRElem> fiber_points;
        std::vector<swgr::algebra::GRElem> fiber_values;
        fiber_points.reserve(static_cast<std::size_t>(fold_factor));
        fiber_values.reserve(static_cast<std::size_t>(fold_factor));
        for (std::uint64_t offset = 0; offset < fold_factor; ++offset) {
          const std::uint64_t oracle_index =
              base_index + offset * next_domain_size;
          const auto* value_ptr = LookupIndexedValue(indexed_g, oracle_index);
          if (value_ptr == nullptr) {
            return false;
          }
          fiber_points.push_back(domain.element(oracle_index));
          fiber_values.push_back(*value_ptr);
        }
        folded_values->push_back(swgr::poly_utils::fold_eval_k(
            fiber_points, fiber_values, folding_alpha));
      }
      return true;
    });
  } catch (...) {
    return false;
  }
}

bool VerifySparseProofSuffix(
    const FriParameters& params, const FriInstance& instance,
    const FriProof& proof, swgr::crypto::Transcript& transcript,
    std::size_t round_offset, std::vector<std::uint64_t> carried_positions,
    std::vector<swgr::algebra::GRElem> carried_values,
    swgr::ProofStatistics* stats) {
  swgr::ProofStatistics local_stats;
  try {
    const std::size_t fold_rounds =
        folding_round_count(instance, params.fold_factor, params.stop_degree);
    if (proof.rounds.empty()) {
      if (!proof.oracle_roots.empty() ||
          proof.final_polynomial.degree() > instance.claimed_degree ||
          carried_positions.size() != carried_values.size()) {
        return false;
      }
      const auto algebra_start = std::chrono::steady_clock::now();
      for (std::size_t i = 0; i < carried_positions.size(); ++i) {
        const auto expected = proof.final_polynomial.evaluate(
            instance.domain.context(),
            instance.domain.element(carried_positions[i]));
        if (expected != carried_values[i]) {
          return false;
        }
      }
      local_stats.verifier_algebra_ms += ElapsedMilliseconds(
          algebra_start, std::chrono::steady_clock::now());
      if (stats != nullptr) {
        *stats = local_stats;
      }
      return true;
    }

    if (proof.rounds.size() != fold_rounds + 1U ||
        proof.oracle_roots.size() != proof.rounds.size() ||
        proof.stats.prover_rounds != fold_rounds) {
      return false;
    }

    Domain current_domain = instance.domain;
    std::uint64_t current_degree = instance.claimed_degree;
    const auto query_rounds = resolve_query_rounds_metadata(params, instance);

    for (std::size_t round_index = 0; round_index < fold_rounds; ++round_index) {
      const auto& round = proof.rounds[round_index];
      const auto& oracle_root = proof.oracle_roots[round_index];
      const std::uint64_t bundle_count =
          current_domain.size() / params.fold_factor;

      const auto transcript_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(oracle_root);
      const auto folding_alpha = derive_fri_folding_challenge(
          transcript, current_domain.context(),
          RoundLabel("fri.fold_alpha", round_offset + round_index));
      const auto fresh_query_chains = derive_query_positions(
          transcript, RoundLabel("fri.query", round_offset + round_index),
          bundle_count, query_rounds[round_index].fresh_query_count);
      const auto expected_queries = OpenedRoundQueries(
          carried_positions, bundle_count, fresh_query_chains);
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_start, std::chrono::steady_clock::now());

      const auto verify_merkle_start = std::chrono::steady_clock::now();
      if (round.oracle_proof.queried_indices != expected_queries ||
          !swgr::crypto::MerkleTree::verify(
              params.hash_profile, bundle_count, oracle_root,
              round.oracle_proof)) {
        return false;
      }
      local_stats.verifier_merkle_ms += ElapsedMilliseconds(
          verify_merkle_start, std::chrono::steady_clock::now());

      const auto algebra_start = std::chrono::steady_clock::now();
      std::vector<std::vector<swgr::algebra::GRElem>> bundles;
      if (!DecodeBundles(current_domain.context(), round.oracle_proof,
                         params.fold_factor, &bundles) ||
          !VerifyCarriedValues(carried_positions, carried_values, bundle_count,
                               expected_queries, bundles)) {
        return false;
      }
      local_stats.verifier_algebra_ms += ElapsedMilliseconds(
          algebra_start, std::chrono::steady_clock::now());

      const auto query_phase_start = std::chrono::steady_clock::now();
      std::vector<swgr::algebra::GRElem> next_unique_values;
      next_unique_values.reserve(expected_queries.size());
      try {
        current_domain.context().with_ntl_context([&] {
          for (std::size_t i = 0; i < expected_queries.size(); ++i) {
            std::vector<swgr::algebra::GRElem> fiber_points;
            fiber_points.reserve(static_cast<std::size_t>(params.fold_factor));
            for (std::uint64_t offset = 0; offset < params.fold_factor; ++offset) {
              fiber_points.push_back(current_domain.element(
                  expected_queries[i] + offset * bundle_count));
            }
            next_unique_values.push_back(swgr::poly_utils::fold_eval_k(
                fiber_points, bundles[i], folding_alpha));
          }
          return 0;
        });
      } catch (...) {
        return false;
      }
      const auto next_positions = NextCarriedQueryChains(
          carried_positions, bundle_count, fresh_query_chains);
      std::vector<swgr::algebra::GRElem> next_values;
      next_values.reserve(next_positions.size());
      for (const auto position : next_positions) {
        const auto it = std::lower_bound(
            expected_queries.begin(), expected_queries.end(), position);
        if (it == expected_queries.end() || *it != position) {
          return false;
        }
        next_values.push_back(
            next_unique_values[static_cast<std::size_t>(
                it - expected_queries.begin())]);
      }
      local_stats.verifier_query_phase_ms += ElapsedMilliseconds(
          query_phase_start, std::chrono::steady_clock::now());

      carried_positions = next_positions;
      carried_values = std::move(next_values);
      current_domain = current_domain.pow_map(params.fold_factor);
      current_degree /= params.fold_factor;
    }

    const auto& final_round = proof.rounds.back();
    const auto& final_root = proof.oracle_roots.back();
    auto final_query_chains = carried_positions;
    if (final_query_chains.empty()) {
      const auto transcript_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(final_root);
      final_query_chains = DeriveTerminalQueryChains(
          transcript, params,
          FriInstance{
              .domain = current_domain,
              .claimed_degree = current_degree,
          },
          round_offset + fold_rounds);
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_start, std::chrono::steady_clock::now());
    }
    const auto final_queries = UniqueSorted(final_query_chains);

    const auto verify_merkle_start = std::chrono::steady_clock::now();
    if (final_round.oracle_proof.queried_indices != final_queries ||
        !swgr::crypto::MerkleTree::verify(
            params.hash_profile, current_domain.size(), final_root,
            final_round.oracle_proof)) {
      return false;
    }
    local_stats.verifier_merkle_ms +=
        ElapsedMilliseconds(verify_merkle_start, std::chrono::steady_clock::now());

    const auto algebra_start = std::chrono::steady_clock::now();
    if (proof.final_polynomial.degree() > current_degree) {
      return false;
    }
    std::vector<std::vector<swgr::algebra::GRElem>> final_values;
    if (!DecodeBundles(current_domain.context(), final_round.oracle_proof, 1,
                       &final_values) ||
        !VerifyCarriedValues(carried_positions, carried_values,
                             current_domain.size(), final_queries, final_values) ||
        !EvaluateFinalPolynomial(current_domain, proof.final_polynomial,
                                 final_queries, final_values)) {
      return false;
    }
    local_stats.verifier_algebra_ms +=
        ElapsedMilliseconds(algebra_start, std::chrono::steady_clock::now());

    if (stats != nullptr) {
      *stats = local_stats;
    }
    return true;
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
    swgr::crypto::Transcript transcript(params_.hash_profile);

    const auto verify_merkle_start = std::chrono::steady_clock::now();
    const bool committed_ok = swgr::crypto::MerkleTree::verify(
        params_.hash_profile, commitment.domain.size(), commitment.oracle_root,
        opening.proof.committed_oracle_proof);
    local_stats.verifier_merkle_ms += ElapsedMilliseconds(
        verify_merkle_start, std::chrono::steady_clock::now());
    if (!committed_ok) {
      return false;
    }

    std::vector<std::vector<swgr::algebra::GRElem>> committed_values;
    if (!DecodeBundles(ctx, opening.proof.committed_oracle_proof, 1,
                       &committed_values)) {
      return false;
    }
    const auto indexed_f = IndexedSingletonValues(
        opening.proof.committed_oracle_proof.queried_indices, committed_values);

    if (total_rounds == 0) {
      const auto transcript_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(commitment.oracle_root);
      const auto terminal_query_chains =
          DeriveTerminalQueryChains(transcript, params_, reduced_instance, 0);
      const auto terminal_queries = UniqueSorted(terminal_query_chains);
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_start, std::chrono::steady_clock::now());
      if (opening.proof.committed_oracle_proof.queried_indices !=
          terminal_queries) {
        return false;
      }

      std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>> indexed_g;
      const auto algebra_start = std::chrono::steady_clock::now();
      if (!BuildVirtualIndexedValues(commitment.domain, indexed_f, alpha, value,
                                     &indexed_g)) {
        return false;
      }
      std::vector<swgr::algebra::GRElem> carried_values;
      carried_values.reserve(terminal_query_chains.size());
      for (const auto query : terminal_query_chains) {
        const auto* query_value = LookupIndexedValue(indexed_g, query);
        if (query_value == nullptr) {
          return false;
        }
        carried_values.push_back(*query_value);
      }
      local_stats.verifier_algebra_ms += ElapsedMilliseconds(
          algebra_start, std::chrono::steady_clock::now());

      swgr::ProofStatistics suffix_stats;
      if (!VerifySparseProofSuffix(params_, reduced_instance,
                                   opening.proof.quotient_proof, transcript, 0,
                                   terminal_query_chains, carried_values,
                                   &suffix_stats)) {
        return false;
      }
      AccumulateVerifierStats(local_stats, suffix_stats);
      local_stats.verifier_total_ms =
          ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
      if (stats != nullptr) {
        *stats = local_stats;
      }
      return true;
    }

    const auto query_rounds = resolve_query_rounds_metadata(params_, reduced_instance);
    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(commitment.oracle_root);
    const auto folding_alpha = derive_fri_folding_challenge(
        transcript, ctx, RoundLabel("fri.fold_alpha", 0));
    const std::uint64_t next_domain_size =
        reduced_instance.domain.size() / params_.fold_factor;
    const auto first_query_chains = derive_query_positions(
        transcript, RoundLabel("fri.query", 0), next_domain_size,
        query_rounds.front().fresh_query_count);
    local_stats.verifier_transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());

    const auto expected_input_indices =
        ExpandBundleIndices(first_query_chains, next_domain_size,
                            params_.fold_factor);
    if (opening.proof.committed_oracle_proof.queried_indices !=
        expected_input_indices) {
      return false;
    }

    const auto algebra_start = std::chrono::steady_clock::now();
    std::vector<std::pair<std::uint64_t, swgr::algebra::GRElem>> indexed_g;
    if (!BuildVirtualIndexedValues(commitment.domain, indexed_f, alpha, value,
                                   &indexed_g)) {
      return false;
    }
    std::vector<swgr::algebra::GRElem> folded_values;
    if (!FoldVirtualQueries(reduced_instance.domain, params_.fold_factor,
                            first_query_chains, indexed_g, folding_alpha,
                            &folded_values)) {
      return false;
    }
    local_stats.verifier_algebra_ms +=
        ElapsedMilliseconds(algebra_start, std::chrono::steady_clock::now());

    const FriInstance suffix_instance{
        .domain = reduced_instance.domain.pow_map(params_.fold_factor),
        .claimed_degree = reduced_instance.claimed_degree / params_.fold_factor,
    };
    swgr::ProofStatistics suffix_stats;
    if (!VerifySparseProofSuffix(params_, suffix_instance,
                                 opening.proof.quotient_proof, transcript, 1,
                                 first_query_chains, folded_values,
                                 &suffix_stats)) {
      return false;
    }
    AccumulateVerifierStats(local_stats, suffix_stats);
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

bool FriVerifier::verify(const FriCommitment& commitment,
                         const swgr::algebra::GRElem& alpha,
                         const swgr::algebra::GRElem& value,
                         const FriOpeningArtifact& opening,
                         swgr::ProofStatistics* stats) const {
  return verify(commitment, alpha, value, opening.opening, stats);
}

bool FriVerifier::verify(const FriInstance& instance, const FriProof& proof,
                         swgr::ProofStatistics* stats) const {
  swgr::ProofStatistics local_stats;
  const auto verify_start = std::chrono::steady_clock::now();
  if (!validate(params_, instance)) {
    return false;
  }

  swgr::crypto::Transcript transcript(params_.hash_profile);
  if (!VerifySparseProofSuffix(params_, instance, proof, transcript, 0, {}, {},
                               &local_stats)) {
    return false;
  }
  local_stats.verifier_total_ms =
      ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
  if (stats != nullptr) {
    *stats = local_stats;
  }
  return true;
}

bool FriVerifier::verify(const FriInstance& instance,
                         const FriProofWithWitness& artifact,
                         swgr::ProofStatistics* stats) const {
  return verify(instance, artifact.proof, stats);
}

}  // namespace swgr::fri
