#include "fri/prover.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "crypto/fs/transcript.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/quotient.hpp"

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

}  // namespace

FriProver::FriProver(FriParameters params) : params_(std::move(params)) {}

FriCommitment FriProver::commit(
    const FriInstance& instance,
    const swgr::poly_utils::Polynomial& polynomial) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument(
        "fri::FriProver::commit received invalid instance");
  }
  if (polynomial.degree() > instance.claimed_degree) {
    throw std::invalid_argument(
        "fri::FriProver::commit polynomial exceeds claimed degree");
  }

  FriCommitment commitment{
      .domain = instance.domain,
      .degree_bound = instance.claimed_degree,
      .oracle_root = {},
      .stats = {},
  };
  if (!commitment_domain_supported(commitment)) {
    throw std::invalid_argument(
        "fri::FriProver::commit requires an evaluation domain L contained in T");
  }

  const auto commit_start = std::chrono::steady_clock::now();
  const auto encode_start = std::chrono::steady_clock::now();
  const auto oracle = swgr::poly_utils::rs_encode(instance.domain, polynomial);
  commitment.stats.prover_encode_ms =
      ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());
  const auto merkle_start = std::chrono::steady_clock::now();
  commitment.oracle_root =
      build_oracle_tree(params_.hash_profile, instance.domain.context(), oracle, 1)
          .root();
  commitment.stats.prover_merkle_ms =
      ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());
  if (!validate(params_, commitment)) {
    throw std::runtime_error(
        "fri::FriProver::commit produced an invalid commitment");
  }
  commitment.stats.commit_ms =
      ElapsedMilliseconds(commit_start, std::chrono::steady_clock::now());
  commitment.stats.prover_total_ms = commitment.stats.commit_ms;
  commitment.stats.serialized_bytes = serialized_message_bytes(commitment);
  return commitment;
}

FriOpening FriProver::open(const FriCommitment& commitment,
                           const swgr::poly_utils::Polynomial& polynomial,
                           const swgr::algebra::GRElem& alpha) const {
  if (!validate(params_, commitment)) {
    throw std::invalid_argument(
        "fri::FriProver::open received invalid commitment");
  }
  if (!opening_point_valid(commitment, alpha)) {
    throw std::invalid_argument("fri::FriProver::open requires alpha in T");
  }
  if (polynomial.degree() > commitment.degree_bound) {
    throw std::invalid_argument(
        "fri::FriProver::open polynomial exceeds commitment degree bound");
  }

  const auto open_start = std::chrono::steady_clock::now();
  const auto& ctx = commitment.domain.context();
  double encode_ms = 0.0;
  double merkle_ms = 0.0;
  double answer_ms = 0.0;
  double quotient_ms = 0.0;
  double transcript_ms = 0.0;
  double fold_ms = 0.0;
  double query_open_ms = 0.0;
  double query_phase_ms = 0.0;

  const auto encode_start = std::chrono::steady_clock::now();
  const auto committed_oracle =
      swgr::poly_utils::rs_encode(commitment.domain, polynomial);
  encode_ms += ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());

  const auto merkle_start = std::chrono::steady_clock::now();
  const auto commitment_tree =
      build_oracle_tree(params_.hash_profile, ctx, committed_oracle, 1);
  const auto expected_root = commitment_tree.root();
  merkle_ms += ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());
  if (expected_root != commitment.oracle_root) {
    throw std::invalid_argument(
        "fri::FriProver::open polynomial does not match the commitment root");
  }

  const auto answer_start = std::chrono::steady_clock::now();
  const auto value = polynomial.evaluate(ctx, alpha);
  answer_ms += ElapsedMilliseconds(answer_start, std::chrono::steady_clock::now());

  const auto quotient_start = std::chrono::steady_clock::now();
  const auto quotient_polynomial =
      swgr::poly_utils::quotient_polynomial_from_answers(ctx, polynomial, {alpha},
                                                         {value});
  const auto quotient_oracle =
      swgr::poly_utils::rs_encode(opening_instance(commitment).domain,
                                  quotient_polynomial);
  quotient_ms +=
      ElapsedMilliseconds(quotient_start, std::chrono::steady_clock::now());

  FriOpening opening;
  opening.claim.alpha = alpha;
  opening.claim.value = value;

  const FriInstance reduced_instance = opening_instance(commitment);
  if (!validate(params_, reduced_instance)) {
    throw std::invalid_argument(
        "fri::FriProver::open derived an invalid quotient instance");
  }

  const std::size_t total_rounds =
      folding_round_count(reduced_instance, params_.fold_factor, params_.stop_degree);
  swgr::crypto::Transcript transcript(params_.hash_profile);
  transcript.absorb_bytes(commitment.oracle_root);
  AbsorbEvaluationClaim(transcript, ctx, alpha, value);

  if (total_rounds == 0) {
    opening.proof.final_oracle = quotient_oracle;
    opening.proof.revealed_committed_oracle = committed_oracle;
  } else {
    const auto query_rounds = resolve_query_rounds_metadata(params_, reduced_instance);

    Domain current_domain = reduced_instance.domain;
    auto current_oracle = quotient_oracle;
    std::vector<Domain> oracle_domains;
    oracle_domains.reserve(total_rounds);
    std::vector<swgr::crypto::MerkleTree> oracle_trees;
    oracle_trees.reserve(total_rounds);

    for (std::size_t round_index = 0; round_index < total_rounds; ++round_index) {
      const auto transcript_start = std::chrono::steady_clock::now();
      const auto folding_alpha = derive_fri_folding_challenge(
          transcript, ctx, RoundLabel("fri.fold_alpha", round_index));
      transcript_ms +=
          ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());

      const auto fold_start = std::chrono::steady_clock::now();
      current_oracle = swgr::poly_utils::fold_table_k(
          current_domain, current_oracle, params_.fold_factor, folding_alpha);
      fold_ms += ElapsedMilliseconds(fold_start, std::chrono::steady_clock::now());
      current_domain = current_domain.pow_map(params_.fold_factor);

      const auto round_merkle_start = std::chrono::steady_clock::now();
      auto oracle_tree =
          build_oracle_tree(params_.hash_profile, ctx, current_oracle, 1);
      merkle_ms += ElapsedMilliseconds(round_merkle_start,
                                       std::chrono::steady_clock::now());

      opening.proof.oracle_roots.push_back(oracle_tree.root());
      oracle_domains.push_back(current_domain);
      oracle_trees.push_back(std::move(oracle_tree));

      const auto absorb_start = std::chrono::steady_clock::now();
      transcript.absorb_bytes(opening.proof.oracle_roots.back());
      transcript_ms +=
          ElapsedMilliseconds(absorb_start, std::chrono::steady_clock::now());
    }

    opening.proof.final_oracle = current_oracle;

    const auto transcript_start = std::chrono::steady_clock::now();
    auto current_query_chains = derive_query_positions(
        transcript, RoundLabel("fri.query", 0), oracle_domains.front().size(),
        query_rounds.front().fresh_query_count);
    transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());

    for (std::size_t round_index = 0; round_index < total_rounds; ++round_index) {
      FriRoundProof round;
      const Domain& child_domain = oracle_domains[round_index];
      const auto parent_indices = ExpandFiberIndices(
          current_query_chains, child_domain.size(), params_.fold_factor);

      const auto query_start = std::chrono::steady_clock::now();
      if (round_index == 0) {
        round.parent_oracle_proof = commitment_tree.open(parent_indices);
      } else {
        round.parent_oracle_proof =
            oracle_trees[round_index - 1].open(parent_indices);
      }
      if (round_index + 1U < total_rounds) {
        round.child_oracle_proof =
            oracle_trees[round_index].open(UniqueSorted(current_query_chains));
      }
      const double open_elapsed =
          ElapsedMilliseconds(query_start, std::chrono::steady_clock::now());
      query_open_ms += open_elapsed;
      query_phase_ms += open_elapsed;
      opening.proof.rounds.push_back(std::move(round));

      if (round_index + 1U < total_rounds) {
        current_query_chains = CarryToBundleQueryChains(
            current_query_chains, oracle_domains[round_index + 1].size());
      }
    }
  }

  opening.proof.stats.prover_rounds = static_cast<std::uint64_t>(total_rounds);
  opening.proof.stats.verifier_hashes = 0;
  for (const auto& round : opening.proof.rounds) {
    opening.proof.stats.verifier_hashes += static_cast<std::uint64_t>(
        round.parent_oracle_proof.sibling_hashes.size() +
        round.child_oracle_proof.sibling_hashes.size());
  }
  opening.proof.stats.serialized_bytes = serialized_message_bytes(ctx, opening);
  opening.proof.stats.prover_encode_ms += encode_ms;
  opening.proof.stats.prover_merkle_ms += merkle_ms;
  opening.proof.stats.prover_answer_ms += answer_ms;
  opening.proof.stats.prover_quotient_ms += quotient_ms;
  opening.proof.stats.prover_transcript_ms += transcript_ms;
  opening.proof.stats.prover_fold_ms += fold_ms;
  opening.proof.stats.prover_query_open_ms += query_open_ms;
  opening.proof.stats.prove_query_phase_ms += query_phase_ms;
  opening.proof.stats.prover_total_ms =
      ElapsedMilliseconds(open_start, std::chrono::steady_clock::now());
  return opening;
}

}  // namespace swgr::fri
