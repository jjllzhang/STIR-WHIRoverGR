#ifndef STIR_WHIR_GR_SOUNDNESS_CONFIGURATOR_HPP_
#define STIR_WHIR_GR_SOUNDNESS_CONFIGURATOR_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../parameters.hpp"

namespace stir_whir_gr::soundness {

struct EngineeringHeuristicResult {
  std::string model;
  std::string scope;
  std::string query_policy;
  std::string pow_policy;
  std::uint64_t effective_security_bits = 0;
  std::vector<std::string> notes;
};

bool validate_manual_queries(
    const std::vector<std::uint64_t>& manual_queries);
std::uint64_t effective_security_bits(std::uint64_t lambda_target,
                                      std::uint64_t pow_bits);
double heuristic_eta(stir_whir_gr::SecurityMode mode, double rho);
std::uint64_t heuristic_base_query_count(stir_whir_gr::SecurityMode mode, double rho,
                                         std::uint64_t effective_bits);
std::uint64_t auto_query_count_for_round(stir_whir_gr::SecurityMode mode,
                                         std::uint64_t lambda_target,
                                         std::uint64_t pow_bits, double rho,
                                         std::size_t round_index);

EngineeringHeuristicResult engineering_heuristic_result(
    stir_whir_gr::SecurityMode mode, std::uint64_t lambda_target,
    std::uint64_t pow_bits, bool manual_query_schedule, double rho);

}  // namespace stir_whir_gr::soundness

#endif  // STIR_WHIR_GR_SOUNDNESS_CONFIGURATOR_HPP_
