#ifndef STIR_WHIR_GR_STIR_PARAMETERS_HPP_
#define STIR_WHIR_GR_STIR_PARAMETERS_HPP_

#include <cstdint>
#include <vector>

#include "../parameters.hpp"

namespace stir_whir_gr::stir {

struct StirInstance;

struct RoundQueryScheduleMetadata {
  std::uint64_t requested_query_count = 0;
  std::uint64_t effective_query_count = 0;
  std::uint64_t bundle_count = 0;
  std::uint64_t degree_budget = 0;
  bool cap_applied = false;
};

enum class StirProtocolMode {
  PrototypeEngineering,
  TheoremGr,
};

enum class StirChallengeSampling {
  AmbientRing,
  TeichmullerT,
};

enum class StirOodSamplingMode {
  PrototypeShiftedCoset,
  TheoremExceptionalComplementUnique,
};

struct StirParameters {
  std::uint64_t virtual_fold_factor = 9;
  std::uint64_t shift_power = 3;
  std::uint64_t ood_samples = 2;
  std::vector<std::uint64_t> query_repetitions;
  std::uint64_t stop_degree = 3;
  std::uint64_t lambda_target = 128;
  std::uint64_t pow_bits = 0;
  stir_whir_gr::SecurityMode sec_mode = stir_whir_gr::SecurityMode::ConjectureCapacity;
  stir_whir_gr::HashProfile hash_profile = stir_whir_gr::HashProfile::STIR_NATIVE;
  StirProtocolMode protocol_mode = StirProtocolMode::PrototypeEngineering;
  StirChallengeSampling challenge_sampling = StirChallengeSampling::AmbientRing;
  StirOodSamplingMode ood_sampling =
      StirOodSamplingMode::PrototypeShiftedCoset;
};

bool validate(const StirParameters& params);
bool validate(const StirParameters& params, const StirInstance& instance);
std::vector<RoundQueryScheduleMetadata> resolve_query_schedule_metadata(
    const StirParameters& params, const StirInstance& instance);
std::vector<std::uint64_t> resolve_query_repetitions(
    const StirParameters& params, const StirInstance& instance);

}  // namespace stir_whir_gr::stir

#endif  // STIR_WHIR_GR_STIR_PARAMETERS_HPP_
