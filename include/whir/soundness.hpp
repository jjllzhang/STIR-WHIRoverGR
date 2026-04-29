#ifndef SWGR_WHIR_SOUNDNESS_HPP_
#define SWGR_WHIR_SOUNDNESS_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace swgr::whir {

struct WhirRational {
  std::uint64_t numerator = 0;
  std::uint64_t denominator = 1;
};

struct WhirUniqueDecodingInputs {
  std::uint64_t lambda_target = 128;
  std::uint64_t ring_exponent = 16;  // s in GR(2^s, r).
  std::uint64_t variable_count = 0;  // m.
  std::uint64_t max_layer_width = 1; // bmax.
  WhirRational rho0{1, 3};
  WhirRational theta{1, 2};

  // Optional benchmark guards. A value of 0 means "unbounded".
  std::uint64_t max_extension_degree = 0;
  std::uint64_t max_domain_size = 0;
  std::uint64_t max_n0_search_steps = 100000;
};

struct WhirUniqueDecodingLayer {
  std::uint64_t layer_index = 0;
  std::uint64_t variable_count = 0;
  std::uint64_t width = 0;
  std::uint64_t domain_size = 0;
  std::uint64_t rate_numerator = 0;
  std::uint64_t rate_denominator = 1;
  long double rate = 0.0L;
  long double delta = 0.0L;
  std::uint64_t repetition_count = 0;
  std::uint64_t sumcheck_degree_bound = 0;
  std::string folding_algebra_bound;
};

struct WhirUniqueDecodingPublicParameters {
  std::uint64_t base_prime = 2;
  std::uint64_t ring_exponent = 0;
  std::uint64_t extension_degree = 0;
  std::uint64_t initial_domain_size = 0;
  std::uint64_t variable_count = 0;
  std::vector<std::uint64_t> layer_widths;
  std::vector<std::uint64_t> shift_repetitions;
  std::uint64_t final_repetitions = 0;
  std::vector<std::uint64_t> degree_bounds;
  std::vector<long double> rates;
  std::vector<long double> deltas;
  std::uint64_t lambda_target = 0;
};

struct WhirUniqueDecodingSelection {
  bool feasible = false;
  WhirUniqueDecodingPublicParameters public_params;
  std::uint64_t required_3_adic_power = 0;
  std::uint64_t rdom = 0;
  std::uint64_t rsec = 0;
  std::uint64_t selected_r = 0;
  std::uint64_t repetition_security_bits = 0;
  std::uint64_t effective_security_bits = 0;
  std::string algebraic_bound;
  long double algebraic_error_log2 = 0.0L;
  long double total_error_log2 = 0.0L;
  std::vector<WhirUniqueDecodingLayer> layers;
  std::vector<std::string> notes;
};

WhirUniqueDecodingSelection
select_whir_unique_decoding_parameters(const WhirUniqueDecodingInputs &inputs);

std::uint64_t multiplicative_order_mod_odd(std::uint64_t modulus,
                                           std::uint64_t base);

bool domain_divides_teichmuller_group(std::uint64_t domain_size,
                                      std::uint64_t extension_degree);

} // namespace swgr::whir

#endif // SWGR_WHIR_SOUNDNESS_HPP_
