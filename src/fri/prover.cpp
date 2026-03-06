#include "fri/prover.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"

namespace swgr::fri {

FriProver::FriProver(FriParameters params) : params_(std::move(params)) {}

FriProof FriProver::prove(
    const FriInstance& instance,
    const swgr::poly_utils::Polynomial& polynomial) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument("fri::FriProver::prove received invalid instance");
  }
  if (polynomial.degree() > instance.claimed_degree) {
    throw std::invalid_argument(
        "fri::FriProver::prove polynomial exceeds claimed degree");
  }

  FriProof proof;
  Domain current_domain = instance.domain;
  std::uint64_t current_degree = instance.claimed_degree;
  auto current_oracle = swgr::poly_utils::rs_encode(current_domain, polynomial);

  const std::size_t fold_rounds =
      folding_round_count(instance, params_.fold_factor, params_.stop_degree);
  const auto schedule = query_schedule(fold_rounds, params_.query_repetitions);

  for (std::size_t round_index = 0; round_index < fold_rounds; ++round_index) {
    FriRoundProof round;
    round.round_index = static_cast<std::uint64_t>(round_index);
    round.domain_size = current_domain.size();
    round.oracle_evals = current_oracle;

    const auto oracle_commitment =
        commit_oracle(current_domain.context(), round.oracle_evals);
    round.folding_alpha =
        derive_round_challenge(current_domain.context(), oracle_commitment,
                               static_cast<std::uint64_t>(round_index));
    round.query_positions = derive_query_positions(
        oracle_commitment, static_cast<std::uint64_t>(round_index),
        current_domain.size() / params_.fold_factor, schedule[round_index]);

    proof.oracle_roots.push_back(oracle_commitment);
    proof.rounds.push_back(round);

    current_oracle = swgr::poly_utils::fold_table_k(
        current_domain, current_oracle, params_.fold_factor, round.folding_alpha);
    current_domain = current_domain.pow_map(params_.fold_factor);
    current_degree /= params_.fold_factor;
  }

  FriRoundProof final_round;
  final_round.round_index = static_cast<std::uint64_t>(fold_rounds);
  final_round.domain_size = current_domain.size();
  final_round.folding_alpha = current_domain.context().zero();
  final_round.oracle_evals = current_oracle;
  proof.oracle_roots.push_back(
      commit_oracle(current_domain.context(), final_round.oracle_evals));
  proof.final_polynomial =
      swgr::poly_utils::rs_interpolate(current_domain, final_round.oracle_evals);
  proof.rounds.push_back(std::move(final_round));

  if (proof.final_polynomial.degree() > current_degree) {
    throw std::runtime_error(
        "fri::FriProver::prove terminal polynomial violates degree bound");
  }

  const std::uint64_t elem_bytes =
      static_cast<std::uint64_t>(instance.domain.context().elem_bytes());
  std::uint64_t serialized_bytes = 0;
  for (const auto& oracle_root : proof.oracle_roots) {
    serialized_bytes += static_cast<std::uint64_t>(oracle_root.size());
  }
  for (const auto& round : proof.rounds) {
    serialized_bytes += static_cast<std::uint64_t>(round.oracle_evals.size()) *
                        elem_bytes;
    serialized_bytes += static_cast<std::uint64_t>(round.query_positions.size()) *
                        sizeof(std::uint64_t);
  }
  serialized_bytes +=
      static_cast<std::uint64_t>(proof.final_polynomial.coefficients().size()) *
      elem_bytes;

  proof.stats.prover_rounds = static_cast<std::uint64_t>(fold_rounds);
  proof.stats.verifier_hashes = 0;
  proof.stats.serialized_bytes = serialized_bytes;
  return proof;
}

}  // namespace swgr::fri
