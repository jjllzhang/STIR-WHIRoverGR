#ifndef SWGR_LDT_HPP_
#define SWGR_LDT_HPP_

#include <cstdint>
#include <string>

namespace swgr {

struct ProofStatistics {
  std::uint64_t prover_rounds = 0;
  std::uint64_t verifier_hashes = 0;
  std::uint64_t serialized_bytes = 0;
};

struct EstimateResult {
  std::uint64_t argument_bytes = 0;
  std::uint64_t verifier_hashes = 0;
  std::string round_breakdown_json = "{}";
};

}  // namespace swgr

#endif  // SWGR_LDT_HPP_
