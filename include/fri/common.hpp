#ifndef SWGR_FRI_COMMON_HPP_
#define SWGR_FRI_COMMON_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "domain.hpp"
#include "ldt.hpp"
#include "poly_utils/polynomial.hpp"

namespace swgr::fri {

struct FriInstance {
  Domain domain;
  std::uint64_t claimed_degree = 0;
};

struct FriRoundState {
  std::uint64_t round_index = 0;
  std::uint64_t fold_factor = 0;
};

struct FriRoundProof {
  std::uint64_t round_index = 0;
  std::uint64_t domain_size = 0;
  swgr::algebra::GRElem folding_alpha;
  std::vector<std::uint64_t> query_positions;
  std::vector<swgr::algebra::GRElem> oracle_evals;
};

struct FriProof {
  std::vector<FriRoundProof> rounds;
  swgr::poly_utils::Polynomial final_polynomial;
  std::vector<std::vector<std::uint8_t>> oracle_roots;
  swgr::ProofStatistics stats;
};

std::size_t folding_round_count(const FriInstance& instance,
                                std::uint64_t fold_factor,
                                std::uint64_t stop_degree);

std::vector<std::uint64_t> query_schedule(
    std::size_t folding_rounds, const std::vector<std::uint64_t>& configured);

std::vector<std::uint8_t> commit_oracle(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& oracle_evals);

swgr::algebra::GRElem derive_round_challenge(
    const swgr::algebra::GRContext& ctx,
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index);

std::vector<std::uint64_t> derive_query_positions(
    const std::vector<std::uint8_t>& oracle_commitment,
    std::uint64_t round_index, std::uint64_t modulus,
    std::uint64_t query_count);

std::string estimate_breakdown_json(
    const std::vector<std::string>& round_entries,
    std::uint64_t final_polynomial_bytes);

}  // namespace swgr::fri

#endif  // SWGR_FRI_COMMON_HPP_
