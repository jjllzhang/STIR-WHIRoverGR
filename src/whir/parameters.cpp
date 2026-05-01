#include "whir/parameters.hpp"

#include <NTL/ZZ_pE.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace stir_whir_gr::whir {
namespace {

bool SameRing(const stir_whir_gr::algebra::GRContext& lhs,
              const stir_whir_gr::algebra::GRContext& rhs) {
  const auto& lhs_cfg = lhs.config();
  const auto& rhs_cfg = rhs.config();
  return lhs_cfg.p == rhs_cfg.p && lhs_cfg.k_exp == rhs_cfg.k_exp &&
         lhs_cfg.r == rhs_cfg.r;
}

bool Pow3Checked(std::uint64_t exponent, std::uint64_t* out) {
  std::uint64_t value = 1;
  for (std::uint64_t i = 0; i < exponent; ++i) {
    if (value > std::numeric_limits<std::uint64_t>::max() / 3U) {
      return false;
    }
    value *= 3U;
  }
  *out = value;
  return true;
}

}  // namespace

bool validate(const WhirParameters& params) {
  return params.folding_factor > 0 && params.lambda_target > 0;
}

bool validate(const WhirPublicParameters& pp) {
  if (!pp.ctx || pp.ctx->config().p != 2 || pp.variable_count == 0 ||
      pp.lambda_target == 0 || pp.layer_widths.empty()) {
    return false;
  }
  if (!SameRing(*pp.ctx, pp.initial_domain.context()) ||
      !pp.initial_domain.is_teichmuller_subset()) {
    return false;
  }
  if (pp.shift_repetitions.size() != pp.layer_widths.size() ||
      pp.degree_bounds.size() != pp.layer_widths.size() ||
      pp.deltas.size() != pp.layer_widths.size()) {
    return false;
  }

  std::uint64_t summed_width = 0;
  std::uint64_t live_variables = pp.variable_count;
  std::uint64_t domain_size = pp.initial_domain.size();
  for (std::size_t layer = 0; layer < pp.layer_widths.size(); ++layer) {
    const std::uint64_t width = pp.layer_widths[layer];
    if (width == 0 || width > live_variables || pp.degree_bounds[layer] == 0 ||
        pp.shift_repetitions[layer] == 0) {
      return false;
    }
    std::uint64_t width_divisor = 0;
    if (!Pow3Checked(width, &width_divisor) ||
        domain_size % width_divisor != 0) {
      return false;
    }
    std::uint64_t live_code_size = 0;
    if (!Pow3Checked(live_variables, &live_code_size) ||
        live_code_size >= domain_size) {
      return false;
    }
    const long double rho = static_cast<long double>(live_code_size) /
                            static_cast<long double>(domain_size);
    if (!(pp.deltas[layer] > 0.0L) ||
        !(pp.deltas[layer] <= 0.5L * (1.0L - rho))) {
      return false;
    }

    summed_width += width;
    live_variables -= width;
    if (domain_size % 3U != 0) {
      return false;
    }
    domain_size /= 3U;
  }
  if (summed_width != pp.variable_count || live_variables != 0) {
    return false;
  }

  return pp.ctx->with_ntl_context([&] {
    if (!pp.ctx->is_unit(pp.omega) || pp.omega == pp.ctx->one() ||
        NTL::power(pp.omega, 3L) != pp.ctx->one()) {
      return false;
    }
    return pp.ternary_grid[0] == pp.ctx->one() &&
           pp.ternary_grid[1] == pp.omega &&
           pp.ternary_grid[2] == pp.omega * pp.omega;
  });
}

bool validate(const WhirParameters& params, const WhirPublicParameters& pp) {
  return validate(params) && validate(pp) &&
         params.lambda_target == pp.lambda_target &&
         params.hash_profile == pp.hash_profile;
}

bool validate(const WhirParameters& params, const WhirCommitment& commitment) {
  return validate(params, commitment.public_params) &&
         !commitment.oracle_root.empty();
}

}  // namespace stir_whir_gr::whir
