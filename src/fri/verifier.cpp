#include "fri/verifier.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "crypto/fs/transcript.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"

#include <algorithm>
#include <utility>

#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"

namespace swgr::fri {
namespace {

std::vector<std::uint64_t> UniqueSorted(const std::vector<std::uint64_t>& values) {
  std::vector<std::uint64_t> unique = values;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  return unique;
}

std::string RoundLabel(const char* prefix, std::size_t round_index) {
  return std::string(prefix) + ":" + std::to_string(round_index);
}

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

}  // namespace

FriVerifier::FriVerifier(FriParameters params) : params_(std::move(params)) {}

bool FriVerifier::verify(const FriInstance& instance,
                         const FriProof& proof,
                         swgr::ProofStatistics* stats) const {
  swgr::ProofStatistics local_stats;
  const auto verify_start = std::chrono::steady_clock::now();
  try {
    if (!validate(params_, instance)) {
      return false;
    }

    const std::size_t fold_rounds =
        folding_round_count(instance, params_.fold_factor, params_.stop_degree);
    if (proof.rounds.size() != fold_rounds + 1 ||
        proof.oracle_roots.size() != proof.rounds.size() ||
        proof.stats.prover_rounds != fold_rounds) {
      return false;
    }

    const auto query_rounds = resolve_query_rounds_metadata(params_, instance);
    Domain current_domain = instance.domain;
    std::uint64_t current_degree = instance.claimed_degree;
    swgr::crypto::Transcript transcript(params_.hash_profile);

    for (std::size_t round_index = 0; round_index < proof.rounds.size();
         ++round_index) {
      const auto& round = proof.rounds[round_index];
      const bool is_terminal = (round_index == fold_rounds);
      const std::uint64_t bundle_size = is_terminal ? 1 : params_.fold_factor;
      // Phase 0 baseline: the verifier still needs the full round oracle table
      // to rebuild the Merkle root before any sparse-opening slimming lands.
      const auto merkle_start = std::chrono::steady_clock::now();
      const auto oracle_tree = build_oracle_tree(
          params_.hash_profile, current_domain.context(), round.oracle_evals,
          bundle_size);
      const auto recomputed_commitment = oracle_tree.root();
      local_stats.verifier_merkle_ms +=
          ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());
      if (round.round_index != round_index ||
          round.domain_size != current_domain.size() ||
          round.oracle_evals.size() != current_domain.size() ||
          recomputed_commitment != proof.oracle_roots[round_index]) {
        return false;
      }

      if (is_terminal) {
        if (!round.query_positions.empty() ||
            proof.final_polynomial.degree() > current_degree) {
          return false;
        }
        const auto algebra_start = std::chrono::steady_clock::now();
        const auto expected_terminal =
            swgr::poly_utils::rs_encode(current_domain, proof.final_polynomial);
        local_stats.verifier_algebra_ms +=
            ElapsedMilliseconds(algebra_start, std::chrono::steady_clock::now());
        local_stats.verifier_total_ms =
            ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
        if (stats != nullptr) {
          *stats = local_stats;
        }
        // Phase 0 baseline: terminal verification still compares the full last
        // RS table against `round.oracle_evals`.
        return expected_terminal == round.oracle_evals;
      }

      const auto transcript_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(proof.oracle_roots[round_index]);
      const auto expected_alpha = derive_round_challenge(
          transcript, current_domain.context(),
          RoundLabel("fri.fold_alpha", round_index));
      if (round.folding_alpha != expected_alpha) {
        return false;
      }

      const std::uint64_t next_domain_size =
          current_domain.size() / params_.fold_factor;
      const auto expected_queries = derive_query_positions(
          transcript, RoundLabel("fri.query", round_index), next_domain_size,
          query_rounds[round_index].effective_query_count);
      local_stats.verifier_transcript_ms += ElapsedMilliseconds(
          transcript_start, std::chrono::steady_clock::now());
      if (round.query_positions != expected_queries) {
        return false;
      }
      const auto expected_unique_queries = UniqueSorted(expected_queries);
      const auto verify_merkle_start = std::chrono::steady_clock::now();
      if (round.oracle_proof.queried_indices != expected_unique_queries ||
          round.oracle_proof.leaf_payloads.size() != expected_unique_queries.size() ||
          !swgr::crypto::MerkleTree::verify(
              params_.hash_profile, next_domain_size,
              proof.oracle_roots[round_index], round.oracle_proof)) {
        return false;
      }
      local_stats.verifier_merkle_ms += ElapsedMilliseconds(
          verify_merkle_start, std::chrono::steady_clock::now());
      const auto algebra_start = std::chrono::steady_clock::now();
      // Phase 0 baseline: queried Merkle payloads are checked by reserializing
      // the corresponding bundles out of `round.oracle_evals`.
      for (std::size_t i = 0; i < expected_unique_queries.size(); ++i) {
        if (round.oracle_proof.leaf_payloads[i] !=
            serialize_oracle_bundle(current_domain.context(), round.oracle_evals,
                                   params_.fold_factor,
                                   expected_unique_queries[i])) {
          return false;
        }
      }

      const auto& next_round = proof.rounds[round_index + 1];
      if (next_round.domain_size != next_domain_size ||
          next_round.oracle_evals.size() != next_domain_size) {
        return false;
      }

      const auto query_phase_start = std::chrono::steady_clock::now();
      // Phase 0 baseline: folding consistency also reads fiber values directly
      // from `round.oracle_evals` and compares them against the next round
      // table, so removing this field breaks both query and transition checks.
      for (const auto base_index : round.query_positions) {
        std::vector<swgr::algebra::GRElem> fiber_points;
        std::vector<swgr::algebra::GRElem> fiber_values;
        fiber_points.reserve(static_cast<std::size_t>(params_.fold_factor));
        fiber_values.reserve(static_cast<std::size_t>(params_.fold_factor));

        for (std::uint64_t fiber_offset = 0; fiber_offset < params_.fold_factor;
             ++fiber_offset) {
          const std::uint64_t oracle_index =
              base_index + fiber_offset * next_domain_size;
          fiber_points.push_back(current_domain.element(oracle_index));
          fiber_values.push_back(
              round.oracle_evals[static_cast<std::size_t>(oracle_index)]);
        }

        const auto folded_value = current_domain.context().with_ntl_context(
            [&] {
              return swgr::poly_utils::fold_eval_k(
                  fiber_points, fiber_values, round.folding_alpha);
            });
        if (folded_value !=
            next_round.oracle_evals[static_cast<std::size_t>(base_index)]) {
          return false;
        }
      }
      local_stats.verifier_query_phase_ms += ElapsedMilliseconds(
          query_phase_start, std::chrono::steady_clock::now());
      local_stats.verifier_algebra_ms +=
          ElapsedMilliseconds(algebra_start, query_phase_start);

      current_domain = current_domain.pow_map(params_.fold_factor);
      current_degree /= params_.fold_factor;
    }
  } catch (...) {
    return false;
  }

  local_stats.verifier_total_ms =
      ElapsedMilliseconds(verify_start, std::chrono::steady_clock::now());
  if (stats != nullptr) {
    *stats = local_stats;
  }
  return false;
}

}  // namespace swgr::fri
