#ifndef SWGR_STIR_COMMON_HPP_
#define SWGR_STIR_COMMON_HPP_

#include <cstdint>
#include <vector>

#include "ldt.hpp"

namespace swgr::stir {

struct StirRoundState {
  std::uint64_t round_index = 0;
  std::uint64_t virtual_fold_factor = 9;
  std::uint64_t shift_power = 3;
};

struct StirProof {
  std::vector<std::vector<std::uint8_t>> oracle_roots;
  swgr::ProofStatistics stats;
};

}  // namespace swgr::stir

#endif  // SWGR_STIR_COMMON_HPP_
