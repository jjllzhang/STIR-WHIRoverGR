#include "whir/prover.hpp"

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "utils.hpp"

namespace swgr::whir {
namespace {

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

std::vector<swgr::algebra::GRElem> EncodeInitialOracle(
    const WhirPublicParameters& pp,
    const MultiQuadraticPolynomial& polynomial) {
  const auto& domain = pp.initial_domain;
  std::vector<swgr::algebra::GRElem> oracle;
  oracle.reserve(static_cast<std::size_t>(domain.size()));
  for (std::uint64_t index = 0; index < domain.size(); ++index) {
    oracle.push_back(polynomial.evaluate_pow(*pp.ctx, domain.element(index)));
  }
  return oracle;
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

WhirProof WhirProver::prove() const {
  (void)params_;
  throw_unimplemented("whir::WhirProver::prove");
}

}  // namespace swgr::whir
