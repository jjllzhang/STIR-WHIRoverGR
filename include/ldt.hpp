#ifndef SWGR_LDT_HPP_
#define SWGR_LDT_HPP_

#include <cstdint>
#include <string>

namespace swgr {

struct ProofStatistics {
  std::uint64_t prover_rounds = 0;
  std::uint64_t verifier_hashes = 0;
  std::uint64_t serialized_bytes = 0;
  double commit_ms = 0.0;
  double prove_query_phase_ms = 0.0;
  double prover_total_ms = 0.0;
  double prover_encode_ms = 0.0;
  double prover_merkle_ms = 0.0;
  double prover_transcript_ms = 0.0;
  double prover_fold_ms = 0.0;
  double prover_interpolate_ms = 0.0;
  double prover_query_open_ms = 0.0;
  double prover_ood_ms = 0.0;
  double prover_answer_ms = 0.0;
  double prover_quotient_ms = 0.0;
  double prover_degree_correction_ms = 0.0;
  double verifier_merkle_ms = 0.0;
  double verifier_transcript_ms = 0.0;
  double verifier_query_phase_ms = 0.0;
  double verifier_algebra_ms = 0.0;
  double verifier_total_ms = 0.0;
};

struct EstimateResult {
  std::uint64_t argument_bytes = 0;
  std::uint64_t verifier_hashes = 0;
  std::string round_breakdown_json = "{}";
};

}  // namespace swgr

#endif  // SWGR_LDT_HPP_
