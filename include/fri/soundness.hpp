#ifndef SWGR_FRI_SOUNDNESS_HPP_
#define SWGR_FRI_SOUNDNESS_HPP_

#include <cstdint>

namespace swgr::fri {

// Theorem-facing standalone FRI PCS soundness inputs for
// epsilon_rbr^FRI = max(s * ell / 2^r, (1 - delta)^m).
struct StandaloneFriSoundnessInputs {
  std::uint64_t base_prime = 2;
  std::uint64_t ring_extension_degree = 0;
  std::uint64_t domain_size = 0;
  std::uint64_t fold_factor = 0;
  std::uint64_t quotient_code_dimension = 0;
  std::uint64_t lambda_target = 0;
};

struct StandaloneFriSoundnessAnalysis {
  std::uint64_t agreement_radius = 0;
  std::uint64_t delta_numerator = 0;
  std::uint64_t delta_denominator = 1;
  bool span_term_within_target = false;
  std::uint64_t minimum_repetition_count = 0;
};

StandaloneFriSoundnessAnalysis analyze_standalone_soundness(
    const StandaloneFriSoundnessInputs& inputs);

}  // namespace swgr::fri

#endif  // SWGR_FRI_SOUNDNESS_HPP_
