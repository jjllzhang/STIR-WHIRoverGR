#include "fri/verifier.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"

namespace swgr::fri {

FriVerifier::FriVerifier(FriParameters params) : params_(std::move(params)) {}

bool FriVerifier::verify(const FriInstance& instance,
                         const FriProof& proof) const {
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

    const auto schedule = query_schedule(fold_rounds, params_.query_repetitions);
    Domain current_domain = instance.domain;
    std::uint64_t current_degree = instance.claimed_degree;

    for (std::size_t round_index = 0; round_index < proof.rounds.size();
         ++round_index) {
      const auto& round = proof.rounds[round_index];
      const auto recomputed_commitment =
          commit_oracle(current_domain.context(), round.oracle_evals);
      if (round.round_index != round_index ||
          round.domain_size != current_domain.size() ||
          round.oracle_evals.size() != current_domain.size() ||
          recomputed_commitment != proof.oracle_roots[round_index]) {
        return false;
      }

      const bool is_terminal = (round_index == fold_rounds);
      if (is_terminal) {
        if (!round.query_positions.empty() ||
            proof.final_polynomial.degree() > current_degree) {
          return false;
        }
        const auto expected_terminal =
            swgr::poly_utils::rs_encode(current_domain, proof.final_polynomial);
        return expected_terminal == round.oracle_evals;
      }

      const auto expected_alpha = derive_round_challenge(
          current_domain.context(), proof.oracle_roots[round_index],
          static_cast<std::uint64_t>(round_index));
      if (round.folding_alpha != expected_alpha) {
        return false;
      }

      const std::uint64_t next_domain_size =
          current_domain.size() / params_.fold_factor;
      const auto expected_queries = derive_query_positions(
          proof.oracle_roots[round_index],
          static_cast<std::uint64_t>(round_index), next_domain_size,
          schedule[round_index]);
      if (round.query_positions != expected_queries) {
        return false;
      }

      const auto& next_round = proof.rounds[round_index + 1];
      if (next_round.domain_size != next_domain_size ||
          next_round.oracle_evals.size() != next_domain_size) {
        return false;
      }

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

      current_domain = current_domain.pow_map(params_.fold_factor);
      current_degree /= params_.fold_factor;
    }
  } catch (...) {
    return false;
  }

  return false;
}

}  // namespace swgr::fri
