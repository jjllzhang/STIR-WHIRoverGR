#ifndef SWGR_WHIR_COMMON_HPP_
#define SWGR_WHIR_COMMON_HPP_

#include <cstdint>
#include <vector>

#include "ldt.hpp"

namespace swgr::whir {

struct WhirProof {
  std::vector<std::vector<std::uint8_t>> oracle_roots;
  swgr::ProofStatistics stats;
};

}  // namespace swgr::whir

#endif  // SWGR_WHIR_COMMON_HPP_
