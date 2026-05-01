#ifndef STIR_WHIR_GR_STIR_SOUNDNESS_HPP_
#define STIR_WHIR_GR_STIR_SOUNDNESS_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "stir/common.hpp"
#include "stir/parameters.hpp"

namespace stir_whir_gr::stir {

enum class StirTheoremSoundnessFlavor {
  GrHalfGapUniqueOod,
};

struct StirRoundTheoremSoundnessTerm {
  std::size_t round_index = 0;
  long double epsilon_out = 0.0L;
  long double epsilon_shift = 0.0L;
  std::uint64_t degree_bound = 0;
  std::uint64_t domain_size = 0;
  std::uint64_t effective_query_count = 0;
  std::vector<std::string> notes;
};

struct StirTheoremSoundnessAnalysis {
  StirTheoremSoundnessFlavor flavor =
      StirTheoremSoundnessFlavor::GrHalfGapUniqueOod;
  bool feasible = false;
  long double epsilon_fold = 0.0L;
  long double epsilon_fin = 0.0L;
  std::vector<StirRoundTheoremSoundnessTerm> rounds;
  std::uint64_t effective_security_bits = 0;
  std::string proximity_gap_model;
  std::string ood_model;
  std::vector<std::string> assumptions;
};

struct StirTheoremQuerySolveResult {
  bool feasible = false;
  std::vector<std::uint64_t> query_schedule;
  StirTheoremSoundnessAnalysis analysis;
  std::vector<std::string> notes;
};

StirTheoremSoundnessAnalysis analyze_theorem_soundness(
    const StirParameters& params, const StirInstance& instance);

StirTheoremQuerySolveResult solve_min_query_schedule_for_lambda(
    const StirParameters& params, const StirInstance& instance);

}  // namespace stir_whir_gr::stir

#endif  // STIR_WHIR_GR_STIR_SOUNDNESS_HPP_
