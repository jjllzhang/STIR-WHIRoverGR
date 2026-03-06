#ifndef SWGR_FRI_COMMON_HPP_
#define SWGR_FRI_COMMON_HPP_

#include <cstdint>
#include <vector>

#include "ldt.hpp"

namespace swgr::fri {

struct FriRoundState {
  std::uint64_t round_index = 0;
  std::uint64_t fold_factor = 0;
};

struct FriProof {
  std::vector<std::vector<std::uint8_t>> oracle_roots;
  swgr::ProofStatistics stats;
};

}  // namespace swgr::fri

#endif  // SWGR_FRI_COMMON_HPP_
