#ifndef STIR_WHIR_GR_FRI_PARAMETERS_HPP_
#define STIR_WHIR_GR_FRI_PARAMETERS_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../parameters.hpp"
#include "fri/common.hpp"

namespace stir_whir_gr::fri {

// Current public FRI parameters intentionally expose the paper-facing
// repetition-count semantics rather than the older engineering query schedule.
struct FriParameters {
  std::uint64_t fold_factor = 3;
  std::uint64_t stop_degree = 3;
  std::uint64_t repetition_count = 1;
  stir_whir_gr::HashProfile hash_profile = stir_whir_gr::HashProfile::STIR_NATIVE;
};

struct QueryRoundMetadata {
  std::uint64_t query_chain_count = 0;
  std::uint64_t fresh_query_count = 0;
  std::uint64_t bundle_count = 0;
  bool carries_previous_queries = false;
};

bool validate(const FriParameters& params);
bool validate(const FriParameters& params, const FriInstance& instance);
bool validate(const FriParameters& params, const FriCommitment& commitment);
bool validate(const FriCommitment& commitment, const FriOpeningClaim& claim);
std::uint64_t terminal_query_chain_count(const FriParameters& params);
std::vector<QueryRoundMetadata> resolve_query_rounds_metadata(
    const FriParameters& params, const FriInstance& instance);

}  // namespace stir_whir_gr::fri

#endif  // STIR_WHIR_GR_FRI_PARAMETERS_HPP_
