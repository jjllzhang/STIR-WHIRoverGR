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
#include "soundness/configurator.hpp"

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

std::vector<std::uint64_t> UnionQueries(
    const std::vector<std::uint64_t>& carried,
    const std::vector<std::uint64_t>& fresh) {
  std::vector<std::uint64_t> merged;
  merged.reserve(carried.size() + fresh.size());
  merged.insert(merged.end(), carried.begin(), carried.end());
  merged.insert(merged.end(), fresh.begin(), fresh.end());
  return UniqueSorted(merged);
}

std::vector<std::uint64_t> CarryToBundleQueries(
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
  return UniqueSorted(queries);
}

std::uint64_t AutoTerminalQueryCount(const FriParameters& params,
                                     const FriInstance& instance,
                                     std::size_t round_index) {
  const double rho = static_cast<double>(instance.claimed_degree + 1U) /
                     static_cast<double>(instance.domain.size());
  return swgr::soundness::auto_query_count_for_round(
      params.sec_mode, params.lambda_target, params.pow_bits, rho, round_index);
}

std::vector<std::uint64_t> DeriveTerminalQueries(
    swgr::crypto::Transcript& transcript, const FriParameters& params,
    const FriInstance& instance, std::size_t round_index) {
  std::uint64_t requested = 1;
  if (!params.query_repetitions.empty()) {
    requested = params.query_repetitions[std::min(
        round_index, params.query_repetitions.size() - 1U)];
  } else {
    requested = AutoTerminalQueryCount(params, instance, round_index);
  }
  const std::uint64_t effective =
      std::min(requested, instance.domain.size());
  return UniqueSorted(derive_query_positions(
      transcript, RoundLabel("fri.final_query", round_index),
      instance.domain.size(), effective));
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

struct SparseProofBuildResult {
  FriProof proof;
  FriWitness witness;
};

SparseProofBuildResult BuildSparseProof(
    const FriParameters& params, const FriInstance& instance,
    const std::vector<swgr::algebra::GRElem>& initial_oracle,
    swgr::crypto::Transcript& transcript, std::size_t round_offset,
    std::vector<std::uint64_t> carried_positions, bool collect_witness,
    double* merkle_ms, double* transcript_ms, double* fold_ms,
    double* interpolate_ms, double* query_open_ms, double* query_phase_ms) {
  SparseProofBuildResult result;
  auto& proof = result.proof;
  auto& witness = result.witness;

  Domain current_domain = instance.domain;
  std::uint64_t current_degree = instance.claimed_degree;
  auto current_oracle = initial_oracle;

  const std::size_t fold_rounds =
      folding_round_count(instance, params.fold_factor, params.stop_degree);
  const auto query_rounds = resolve_query_rounds_metadata(params, instance);

  for (std::size_t round_index = 0; round_index < fold_rounds; ++round_index) {
    if (collect_witness) {
      witness.rounds.push_back(FriRoundWitness{.oracle_evals = current_oracle});
    }

    const auto merkle_start = std::chrono::steady_clock::now();
    const auto oracle_tree = build_oracle_tree(
        params.hash_profile, current_domain.context(), current_oracle,
        params.fold_factor);
    const auto oracle_commitment = oracle_tree.root();
    *merkle_ms +=
        ElapsedMilliseconds(merkle_start, std::chrono::steady_clock::now());
    proof.oracle_roots.push_back(oracle_commitment);

    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(oracle_commitment);
    const auto folding_alpha = derive_fri_folding_challenge(
        transcript, current_domain.context(),
        RoundLabel("fri.fold_alpha", round_offset + round_index));
    const std::uint64_t next_domain_size =
        current_domain.size() / params.fold_factor;
    const auto carried_bundle_queries =
        CarryToBundleQueries(carried_positions, next_domain_size);
    const auto fresh_queries = derive_query_positions(
        transcript, RoundLabel("fri.query", round_offset + round_index),
        next_domain_size, query_rounds[round_index].effective_query_count);
    const auto round_queries =
        UnionQueries(carried_bundle_queries, fresh_queries);
    *transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());

    const auto query_start = std::chrono::steady_clock::now();
    proof.rounds.push_back(
        FriRoundProof{.oracle_proof = oracle_tree.open(round_queries)});
    const double open_elapsed =
        ElapsedMilliseconds(query_start, std::chrono::steady_clock::now());
    *query_open_ms += open_elapsed;
    *query_phase_ms += open_elapsed;

    const auto fold_start = std::chrono::steady_clock::now();
    current_oracle = swgr::poly_utils::fold_table_k(
        current_domain, current_oracle, params.fold_factor, folding_alpha);
    *fold_ms += ElapsedMilliseconds(fold_start, std::chrono::steady_clock::now());
    current_domain = current_domain.pow_map(params.fold_factor);
    current_degree /= params.fold_factor;
    carried_positions = proof.rounds.back().oracle_proof.queried_indices;
  }

  if (collect_witness) {
    witness.rounds.push_back(FriRoundWitness{.oracle_evals = current_oracle});
  }

  const auto final_merkle_start = std::chrono::steady_clock::now();
  const auto final_tree = build_oracle_tree(params.hash_profile,
                                            current_domain.context(),
                                            current_oracle, 1);
  const auto final_root = final_tree.root();
  *merkle_ms +=
      ElapsedMilliseconds(final_merkle_start, std::chrono::steady_clock::now());
  proof.oracle_roots.push_back(final_root);

  auto final_queries = carried_positions;
  if (final_queries.empty()) {
    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(final_root);
    final_queries = DeriveTerminalQueries(
        transcript, params,
        FriInstance{
            .domain = current_domain,
            .claimed_degree = current_degree,
        },
        round_offset + fold_rounds);
    *transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());
  }

  const auto final_query_start = std::chrono::steady_clock::now();
  proof.rounds.push_back(
      FriRoundProof{.oracle_proof = final_tree.open(final_queries)});
  const double final_open_elapsed =
      ElapsedMilliseconds(final_query_start, std::chrono::steady_clock::now());
  *query_open_ms += final_open_elapsed;
  *query_phase_ms += final_open_elapsed;

  const auto interpolate_start = std::chrono::steady_clock::now();
  proof.final_polynomial =
      swgr::poly_utils::rs_interpolate(current_domain, current_oracle);
  *interpolate_ms +=
      ElapsedMilliseconds(interpolate_start, std::chrono::steady_clock::now());
  if (proof.final_polynomial.degree() > current_degree) {
    throw std::runtime_error(
        "fri::BuildSparseProof terminal polynomial violates degree bound");
  }

  proof.stats.prover_rounds = static_cast<std::uint64_t>(fold_rounds);
  proof.stats.verifier_hashes = 0;
  for (const auto& round : proof.rounds) {
    proof.stats.verifier_hashes += static_cast<std::uint64_t>(
        round.oracle_proof.sibling_hashes.size());
  }
  proof.stats.serialized_bytes =
      serialized_message_bytes(instance.domain.context(), proof);
  return result;
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
    throw std::invalid_argument("fri::FriProver::open requires alpha in T \\ L");
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
  double interpolate_ms = 0.0;
  double query_open_ms = 0.0;
  double query_phase_ms = 0.0;

  const auto encode_start = std::chrono::steady_clock::now();
  auto committed_oracle = swgr::poly_utils::rs_encode(commitment.domain, polynomial);
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
  const auto quotient_polynomial = swgr::poly_utils::quotient_polynomial_from_answers(
      ctx, polynomial, {alpha}, {value});
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

  if (total_rounds == 0) {
    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(commitment.oracle_root);
    const auto terminal_queries =
        DeriveTerminalQueries(transcript, params_, reduced_instance, 0);
    transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());

    const auto query_start = std::chrono::steady_clock::now();
    opening.proof.committed_oracle_proof = commitment_tree.open(terminal_queries);
    const double open_elapsed =
        ElapsedMilliseconds(query_start, std::chrono::steady_clock::now());
    query_open_ms += open_elapsed;
    query_phase_ms += open_elapsed;

    opening.proof.quotient_proof.final_polynomial = quotient_polynomial;
    opening.proof.quotient_proof.stats.prover_rounds = 0;
    opening.proof.quotient_proof.stats.serialized_bytes =
        serialized_message_bytes(ctx, opening.proof.quotient_proof);
  } else {
    const auto query_rounds = resolve_query_rounds_metadata(params_, reduced_instance);

    const auto transcript_start = std::chrono::steady_clock::now();
    transcript.absorb_bytes(commitment.oracle_root);
    const auto folding_alpha = derive_fri_folding_challenge(
        transcript, ctx, RoundLabel("fri.fold_alpha", 0));
    const std::uint64_t next_domain_size =
        reduced_instance.domain.size() / params_.fold_factor;
    const auto first_queries = UniqueSorted(derive_query_positions(
        transcript, RoundLabel("fri.query", 0), next_domain_size,
        query_rounds.front().effective_query_count));
    transcript_ms +=
        ElapsedMilliseconds(transcript_start, std::chrono::steady_clock::now());

    const auto input_indices =
        ExpandBundleIndices(first_queries, next_domain_size, params_.fold_factor);
    const auto query_start = std::chrono::steady_clock::now();
    opening.proof.committed_oracle_proof = commitment_tree.open(input_indices);
    const double open_elapsed =
        ElapsedMilliseconds(query_start, std::chrono::steady_clock::now());
    query_open_ms += open_elapsed;
    query_phase_ms += open_elapsed;

    const auto virtual_oracle =
        build_virtual_oracle(commitment.domain, committed_oracle, alpha, value);
    const auto fold_start = std::chrono::steady_clock::now();
    const auto first_folded_oracle = swgr::poly_utils::fold_table_k(
        reduced_instance.domain, virtual_oracle, params_.fold_factor,
        folding_alpha);
    fold_ms += ElapsedMilliseconds(fold_start, std::chrono::steady_clock::now());

    const FriInstance suffix_instance{
        .domain = reduced_instance.domain.pow_map(params_.fold_factor),
        .claimed_degree = reduced_instance.claimed_degree / params_.fold_factor,
    };
    auto suffix_result = BuildSparseProof(
        params_, suffix_instance, first_folded_oracle, transcript, 1,
        first_queries, false, &merkle_ms, &transcript_ms, &fold_ms,
        &interpolate_ms, &query_open_ms, &query_phase_ms);
    opening.proof.quotient_proof = std::move(suffix_result.proof);
  }

  opening.proof.stats = opening.proof.quotient_proof.stats;
  opening.proof.stats.prover_rounds =
      static_cast<std::uint64_t>(total_rounds);
  opening.proof.stats.verifier_hashes += static_cast<std::uint64_t>(
      opening.proof.committed_oracle_proof.sibling_hashes.size());
  opening.proof.stats.serialized_bytes = serialized_message_bytes(ctx, opening);
  opening.proof.stats.prover_encode_ms += encode_ms;
  opening.proof.stats.prover_merkle_ms += merkle_ms;
  opening.proof.stats.prover_answer_ms += answer_ms;
  opening.proof.stats.prover_quotient_ms += quotient_ms;
  opening.proof.stats.prover_transcript_ms += transcript_ms;
  opening.proof.stats.prover_fold_ms += fold_ms;
  opening.proof.stats.prover_interpolate_ms += interpolate_ms;
  opening.proof.stats.prover_query_open_ms += query_open_ms;
  opening.proof.stats.prove_query_phase_ms += query_phase_ms;
  opening.proof.stats.prover_total_ms =
      ElapsedMilliseconds(open_start, std::chrono::steady_clock::now());
  return opening;
}

FriOpeningArtifact FriProver::open_with_witness(
    const FriCommitment& commitment,
    const swgr::poly_utils::Polynomial& polynomial,
    const swgr::algebra::GRElem& alpha) const {
  FriOpeningArtifact artifact;
  artifact.opening = open(commitment, polynomial, alpha);
  artifact.witness.committed_oracle_evals =
      swgr::poly_utils::rs_encode(commitment.domain, polynomial);

  const FriInstance reduced_instance = opening_instance(commitment);
  const std::size_t total_rounds =
      folding_round_count(reduced_instance, params_.fold_factor, params_.stop_degree);
  if (total_rounds == 0) {
    return artifact;
  }

  swgr::crypto::Transcript transcript(params_.hash_profile);
  transcript.absorb_bytes(commitment.oracle_root);
  const auto folding_alpha = derive_fri_folding_challenge(
      transcript, commitment.domain.context(), RoundLabel("fri.fold_alpha", 0));
  const std::uint64_t next_domain_size =
      reduced_instance.domain.size() / params_.fold_factor;
  const auto query_rounds = resolve_query_rounds_metadata(params_, reduced_instance);
  const auto first_queries = UniqueSorted(derive_query_positions(
      transcript, RoundLabel("fri.query", 0), next_domain_size,
      query_rounds.front().effective_query_count));
  const auto virtual_oracle = build_virtual_oracle(
      commitment.domain, artifact.witness.committed_oracle_evals, alpha,
      artifact.opening.claim.value);
  const auto first_folded_oracle = swgr::poly_utils::fold_table_k(
      reduced_instance.domain, virtual_oracle, params_.fold_factor,
      folding_alpha);
  const FriInstance suffix_instance{
      .domain = reduced_instance.domain.pow_map(params_.fold_factor),
      .claimed_degree = reduced_instance.claimed_degree / params_.fold_factor,
  };
  double merkle_ms = 0.0;
  double transcript_ms = 0.0;
  double fold_ms = 0.0;
  double interpolate_ms = 0.0;
  double query_open_ms = 0.0;
  double query_phase_ms = 0.0;
  auto suffix_result = BuildSparseProof(
      params_, suffix_instance, first_folded_oracle, transcript, 1, first_queries,
      true, &merkle_ms, &transcript_ms, &fold_ms, &interpolate_ms,
      &query_open_ms, &query_phase_ms);
  artifact.witness.quotient_witness = std::move(suffix_result.witness);
  return artifact;
}

FriProof FriProver::prove(const FriInstance& instance,
                          const swgr::poly_utils::Polynomial& polynomial) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument("fri::FriProver::prove received invalid instance");
  }
  if (polynomial.degree() > instance.claimed_degree) {
    throw std::invalid_argument(
        "fri::FriProver::prove polynomial exceeds claimed degree");
  }

  swgr::crypto::Transcript transcript(params_.hash_profile);
  const auto prover_start = std::chrono::steady_clock::now();
  double encode_ms = 0.0;
  double merkle_ms = 0.0;
  double transcript_ms = 0.0;
  double fold_ms = 0.0;
  double interpolate_ms = 0.0;
  double query_open_ms = 0.0;
  double query_phase_ms = 0.0;

  const auto encode_start = std::chrono::steady_clock::now();
  const auto current_oracle = swgr::poly_utils::rs_encode(instance.domain, polynomial);
  encode_ms += ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());

  auto result = BuildSparseProof(params_, instance, current_oracle, transcript, 0, {},
                                 false, &merkle_ms, &transcript_ms, &fold_ms,
                                 &interpolate_ms, &query_open_ms, &query_phase_ms);
  auto& proof = result.proof;
  proof.stats.prover_encode_ms = encode_ms;
  proof.stats.prover_merkle_ms = merkle_ms;
  proof.stats.prover_transcript_ms = transcript_ms;
  proof.stats.prover_fold_ms = fold_ms;
  proof.stats.prover_interpolate_ms = interpolate_ms;
  proof.stats.prover_query_open_ms = query_open_ms;
  proof.stats.prove_query_phase_ms = query_phase_ms;
  proof.stats.prover_total_ms =
      ElapsedMilliseconds(prover_start, std::chrono::steady_clock::now());
  return proof;
}

FriProofWithWitness FriProver::prove_with_witness(
    const FriInstance& instance,
    const swgr::poly_utils::Polynomial& polynomial) const {
  if (!validate(params_, instance)) {
    throw std::invalid_argument("fri::FriProver::prove received invalid instance");
  }
  if (polynomial.degree() > instance.claimed_degree) {
    throw std::invalid_argument(
        "fri::FriProver::prove polynomial exceeds claimed degree");
  }

  swgr::crypto::Transcript transcript(params_.hash_profile);
  const auto prover_start = std::chrono::steady_clock::now();
  double encode_ms = 0.0;
  double merkle_ms = 0.0;
  double transcript_ms = 0.0;
  double fold_ms = 0.0;
  double interpolate_ms = 0.0;
  double query_open_ms = 0.0;
  double query_phase_ms = 0.0;

  const auto encode_start = std::chrono::steady_clock::now();
  const auto current_oracle = swgr::poly_utils::rs_encode(instance.domain, polynomial);
  encode_ms += ElapsedMilliseconds(encode_start, std::chrono::steady_clock::now());

  auto result = BuildSparseProof(params_, instance, current_oracle, transcript, 0, {},
                                 true, &merkle_ms, &transcript_ms, &fold_ms,
                                 &interpolate_ms, &query_open_ms, &query_phase_ms);
  auto& proof = result.proof;
  proof.stats.prover_encode_ms = encode_ms;
  proof.stats.prover_merkle_ms = merkle_ms;
  proof.stats.prover_transcript_ms = transcript_ms;
  proof.stats.prover_fold_ms = fold_ms;
  proof.stats.prover_interpolate_ms = interpolate_ms;
  proof.stats.prover_query_open_ms = query_open_ms;
  proof.stats.prove_query_phase_ms = query_phase_ms;
  proof.stats.prover_total_ms =
      ElapsedMilliseconds(prover_start, std::chrono::steady_clock::now());

  FriProofWithWitness artifact;
  artifact.proof = std::move(proof);
  artifact.witness = std::move(result.witness);
  return artifact;
}

}  // namespace swgr::fri
