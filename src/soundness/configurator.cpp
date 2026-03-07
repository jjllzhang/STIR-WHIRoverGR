#include "soundness/configurator.hpp"

#include <algorithm>

namespace swgr::soundness {
namespace {

constexpr double kMinRho = 1.0 / 4096.0;
constexpr double kMaxRho = 0.999999;

double clamp_rho(double rho) {
  return std::clamp(rho, kMinRho, kMaxRho);
}

}  // namespace

bool validate_manual_queries(
    const std::vector<std::uint64_t>& manual_queries) {
  for (const auto query_count : manual_queries) {
    if (query_count == 0) {
      return false;
    }
  }
  return true;
}

std::uint64_t effective_security_bits(std::uint64_t lambda_target,
                                      std::uint64_t pow_bits) {
  if (lambda_target > pow_bits) {
    return lambda_target - pow_bits;
  }
  return 1;
}

double heuristic_eta(swgr::SecurityMode mode, double rho) {
  const double clamped_rho = clamp_rho(rho);
  if (mode == swgr::SecurityMode::Conservative) {
    return clamped_rho;
  }
  return std::clamp(clamped_rho / 2.0, kMinRho, kMaxRho);
}

std::uint64_t heuristic_base_query_count(swgr::SecurityMode mode, double rho,
                                         std::uint64_t effective_bits) {
  const double eta = heuristic_eta(mode, rho);
  std::uint64_t base_queries = eta >= (1.0 / 6.0) ? 2U : 1U;
  if (mode == swgr::SecurityMode::Conservative && effective_bits >= 96 &&
      eta >= (1.0 / 6.0)) {
    base_queries = 3U;
  }
  return base_queries;
}

std::uint64_t auto_query_count_for_round(swgr::SecurityMode mode,
                                         std::uint64_t lambda_target,
                                         std::uint64_t pow_bits, double rho,
                                         std::size_t round_index) {
  const std::uint64_t base_queries = heuristic_base_query_count(
      mode, rho, effective_security_bits(lambda_target, pow_bits));
  return base_queries > round_index ? base_queries - round_index : 1U;
}

EngineeringHeuristicResult engineering_heuristic_result(
    swgr::SecurityMode mode, std::uint64_t lambda_target,
    std::uint64_t pow_bits, bool manual_query_schedule, double rho) {
  EngineeringHeuristicResult result;
  result.model = "engineering-heuristic-v1";
  result.query_policy = manual_query_schedule ? "manual" : "auto_heuristic";
  result.pow_policy = "fixed_bits";
  result.effective_security_bits = effective_security_bits(lambda_target, pow_bits);
  result.notes.push_back(
      "Engineering heuristic only; auto queries follow eta-based schedule "
      "with per-round decay.");
  if (manual_query_schedule) {
    result.notes.push_back("Manual query schedule overrides auto heuristic.");
  }
  result.notes.push_back(
      "Effective security bits are reported as lambda_target - pow_bits.");
  result.notes.push_back(
      "GR shift/quotient/degree-correction soundness is not yet fully formalized.");
  if (pow_bits >= lambda_target) {
    result.notes.push_back(
        "Caveat: pow_bits >= lambda_target, effective_security_bits is clamped to 1.");
  }

  const double eta = heuristic_eta(mode, rho);
  if (mode == swgr::SecurityMode::Conservative && eta >= (1.0 / 6.0) &&
      result.effective_security_bits >= 96) {
    result.notes.push_back(
        "Conservative mode may start with 3 queries when effective security is high.");
  }
  return result;
}

}  // namespace swgr::soundness
