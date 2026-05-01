#include "whir/soundness.hpp"

#include <NTL/ZZ.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace swgr::whir {
namespace {

constexpr std::uint64_t kWhirSumcheckDegreeBound = 4;

NTL::ZZ ToZZ(std::uint64_t value) {
  return NTL::conv<NTL::ZZ>(static_cast<unsigned long>(value));
}

std::uint64_t ZZToUint64(const NTL::ZZ &value, const char *label) {
  if (value < 0 || value > NTL::conv<NTL::ZZ>(static_cast<unsigned long>(
                               std::numeric_limits<std::uint64_t>::max()))) {
    throw std::overflow_error(std::string(label) + " exceeds uint64_t");
  }
  return static_cast<std::uint64_t>(NTL::conv<unsigned long>(value));
}

std::string ZZToString(const NTL::ZZ &value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

std::uint64_t CheckedAdd(std::uint64_t lhs, std::uint64_t rhs,
                         const char *label) {
  if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
    throw std::overflow_error(std::string(label) + " exceeds uint64_t");
  }
  return lhs + rhs;
}

std::uint64_t CheckedMul(std::uint64_t lhs, std::uint64_t rhs,
                         const char *label) {
  if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    throw std::overflow_error(std::string(label) + " exceeds uint64_t");
  }
  return lhs * rhs;
}

std::uint64_t CeilDiv(std::uint64_t numerator, std::uint64_t denominator) {
  if (denominator == 0) {
    throw std::invalid_argument("division by zero");
  }
  return numerator / denominator + (numerator % denominator == 0 ? 0U : 1U);
}

std::uint64_t CeilDivZZ(const NTL::ZZ &numerator, std::uint64_t denominator) {
  if (denominator == 0) {
    throw std::invalid_argument("division by zero");
  }
  const NTL::ZZ den = ToZZ(denominator);
  return ZZToUint64((numerator + den - 1) / den, "ceil division result");
}

std::uint64_t CeilLog2U64(std::uint64_t value) {
  if (value <= 1) {
    return 0;
  }
  std::uint64_t bits = 0;
  --value;
  while (value != 0) {
    ++bits;
    value >>= 1U;
  }
  return bits;
}

std::uint64_t CeilLog2ZZ(const NTL::ZZ &value) {
  if (value <= 1) {
    return 0;
  }
  const long bits = NTL::NumBits(value - 1);
  if (bits < 0) {
    throw std::overflow_error("negative bit count");
  }
  return static_cast<std::uint64_t>(bits);
}

long double Log2ZZ(const NTL::ZZ &value) {
  if (value <= 0) {
    throw std::invalid_argument("log2 is undefined for non-positive integers");
  }
  const long bit_count = NTL::NumBits(value);
  const long kept_bits = std::min<long>(bit_count, 63);
  const NTL::ZZ top = value >> (bit_count - kept_bits);
  const auto top_u = static_cast<std::uint64_t>(NTL::conv<unsigned long>(top));
  const long double scaled = static_cast<long double>(top_u) /
                             std::ldexp(1.0L, static_cast<int>(kept_bits - 1));
  return static_cast<long double>(bit_count - 1) + std::log2(scaled);
}

std::uint64_t Pow3Checked(std::uint64_t exponent) {
  std::uint64_t result = 1;
  for (std::uint64_t i = 0; i < exponent; ++i) {
    result = CheckedMul(result, 3, "power of 3");
  }
  return result;
}

void ValidateOpenUnitRational(const WhirRational &value, const char *label) {
  if (value.denominator == 0) {
    throw std::invalid_argument(std::string(label) +
                                " denominator must be non-zero");
  }
  if (value.numerator == 0) {
    throw std::invalid_argument(std::string(label) +
                                " must be greater than zero");
  }
  if (value.numerator >= value.denominator) {
    throw std::invalid_argument(std::string(label) +
                                " must be strictly less than one");
  }
}

void ValidateInputs(const WhirUniqueDecodingInputs &inputs) {
  if (inputs.lambda_target == 0) {
    throw std::invalid_argument("WHIR lambda_target must be non-zero");
  }
  if (inputs.ring_exponent == 0) {
    throw std::invalid_argument("WHIR ring exponent s must be non-zero");
  }
  if (inputs.variable_count == 0) {
    throw std::invalid_argument("WHIR variable count m must be non-zero");
  }
  if (inputs.max_layer_width == 0) {
    throw std::invalid_argument("WHIR bmax must be non-zero");
  }
  if (inputs.max_n0_search_steps == 0) {
    throw std::invalid_argument("WHIR n0 search step guard must be non-zero");
  }
  if (inputs.fixed_extension_degree != 0 && inputs.max_extension_degree != 0 &&
      inputs.fixed_extension_degree > inputs.max_extension_degree) {
    throw std::invalid_argument(
        "WHIR fixed_extension_degree exceeds max_extension_degree guard");
  }
  ValidateOpenUnitRational(inputs.rho0, "WHIR rho0");
}

std::vector<std::uint64_t> LayerWidths(std::uint64_t variable_count,
                                       std::uint64_t max_layer_width) {
  std::vector<std::uint64_t> widths;
  std::uint64_t remaining = variable_count;
  while (remaining != 0) {
    const std::uint64_t width = std::min(max_layer_width, remaining);
    widths.push_back(width);
    remaining -= width;
  }
  return widths;
}

std::uint64_t
RequiredThreeAdicPower(const std::vector<std::uint64_t> &layer_widths) {
  std::uint64_t required = 0;
  for (std::uint64_t layer = 0; layer < layer_widths.size(); ++layer) {
    required = std::max(required, CheckedAdd(layer, layer_widths[layer],
                                             "required 3-adic power"));
  }
  return required;
}

std::uint64_t LowerBoundForN0(const WhirUniqueDecodingInputs &inputs,
                              std::uint64_t pow3_m) {
  const NTL::ZZ numerator = ToZZ(pow3_m) * ToZZ(inputs.rho0.denominator);
  return CeilDivZZ(numerator, inputs.rho0.numerator);
}

std::uint64_t FirstOddMultipleAtLeast(std::uint64_t lower_bound,
                                      std::uint64_t divisor) {
  std::uint64_t quotient = CeilDiv(lower_bound, divisor);
  if ((quotient & 1U) == 0U) {
    quotient = CheckedAdd(quotient, 1, "odd multiple quotient");
  }
  return CheckedMul(quotient, divisor, "n0 candidate");
}

std::uint64_t AddMod(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t modulus) {
  if (lhs >= modulus || rhs >= modulus) {
    throw std::invalid_argument("modular addition inputs must be reduced");
  }
  if (lhs >= modulus - rhs) {
    return lhs - (modulus - rhs);
  }
  return lhs + rhs;
}

std::uint64_t MulMod(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t modulus) {
  std::uint64_t result = 0;
  lhs %= modulus;
  while (rhs != 0) {
    if ((rhs & 1U) != 0U) {
      result = AddMod(result, lhs, modulus);
    }
    rhs >>= 1U;
    if (rhs != 0) {
      lhs = AddMod(lhs, lhs, modulus);
    }
  }
  return result;
}

std::uint64_t PowMod(std::uint64_t base, std::uint64_t exponent,
                     std::uint64_t modulus) {
  if (modulus == 1) {
    return 0;
  }
  std::uint64_t result = 1 % modulus;
  base %= modulus;
  while (exponent != 0) {
    if ((exponent & 1U) != 0U) {
      result = MulMod(result, base, modulus);
    }
    exponent >>= 1U;
    if (exponent != 0) {
      base = MulMod(base, base, modulus);
    }
  }
  return result;
}

std::vector<std::uint64_t> UniquePrimeFactors(std::uint64_t value) {
  std::vector<std::uint64_t> factors;
  if ((value & 1U) == 0U) {
    factors.push_back(2);
    while ((value & 1U) == 0U) {
      value >>= 1U;
    }
  }
  for (std::uint64_t divisor = 3; divisor <= value / divisor; divisor += 2) {
    if (value % divisor != 0) {
      continue;
    }
    factors.push_back(divisor);
    while (value % divisor == 0) {
      value /= divisor;
    }
  }
  if (value > 1) {
    factors.push_back(value);
  }
  return factors;
}

std::uint64_t EulerPhi(std::uint64_t value) {
  std::uint64_t phi = value;
  for (const auto factor : UniquePrimeFactors(value)) {
    phi = (phi / factor) * (factor - 1U);
  }
  return phi;
}

std::uint64_t RepetitionCountForBits(long double delta,
                                     std::uint64_t target_bits) {
  if (!(delta > 0.0L) || !(delta < 1.0L)) {
    throw std::invalid_argument("WHIR delta must lie in (0, 1)");
  }
  const long double denominator = -std::log2(1.0L - delta);
  if (!std::isfinite(denominator) || !(denominator > 0.0L)) {
    throw std::invalid_argument("WHIR repetition denominator is invalid");
  }
  const long double estimate =
      std::ceil(static_cast<long double>(target_bits) / denominator);
  if (!std::isfinite(estimate) || estimate < 1.0L ||
      estimate >
          static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    throw std::overflow_error("WHIR repetition count exceeds uint64_t");
  }

  std::uint64_t repetitions = static_cast<std::uint64_t>(estimate);
  while (static_cast<long double>(repetitions) * denominator <
         static_cast<long double>(target_bits)) {
    repetitions = CheckedAdd(repetitions, 1, "WHIR repetition count");
  }
  while (repetitions > 1 &&
         static_cast<long double>(repetitions - 1U) * denominator >=
             static_cast<long double>(target_bits)) {
    --repetitions;
  }
  return repetitions;
}

long double Log2Sum(const std::vector<long double> &log2_terms) {
  long double max_log = -std::numeric_limits<long double>::infinity();
  for (const auto term : log2_terms) {
    if (std::isfinite(term)) {
      max_log = std::max(max_log, term);
    }
  }
  if (!std::isfinite(max_log)) {
    return max_log;
  }
  long double scaled_sum = 0.0L;
  for (const auto term : log2_terms) {
    if (std::isfinite(term)) {
      scaled_sum += std::exp2(term - max_log);
    }
  }
  return max_log + std::log2(scaled_sum);
}

std::uint64_t SecurityBitsFromLog2Error(long double log2_error) {
  if (!std::isfinite(log2_error) || log2_error >= 0.0L) {
    return 0;
  }
  const long double bits = -log2_error;
  if (bits >=
      static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(std::floor(bits));
}

struct CandidateAnalysis {
  WhirUniqueDecodingSelection selection;
  bool valid = false;
};

CandidateAnalysis
AnalyzeCandidate(const WhirUniqueDecodingInputs &inputs,
                 const std::vector<std::uint64_t> &layer_widths,
                 std::uint64_t n0) {
  CandidateAnalysis candidate;
  auto &selection = candidate.selection;
  selection.public_params.base_prime = 2;
  selection.public_params.ring_exponent = inputs.ring_exponent;
  selection.public_params.initial_domain_size = n0;
  selection.public_params.variable_count = inputs.variable_count;
  selection.public_params.lambda_target = inputs.lambda_target;
  selection.required_3_adic_power = RequiredThreeAdicPower(layer_widths);
  selection.repetition_security_bits = CheckedAdd(
      CheckedAdd(inputs.lambda_target, 1, "WHIR repetition target bits"),
      CeilLog2U64(CheckedAdd(static_cast<std::uint64_t>(layer_widths.size()),
                             1, "WHIR repetition layer count")),
      "WHIR repetition target bits");

  NTL::ZZ algebraic_bound(0);
  std::uint64_t remaining_variables = inputs.variable_count;
  std::vector<long double> log2_error_terms;

  for (std::uint64_t layer = 0; layer < layer_widths.size(); ++layer) {
    const std::uint64_t width = layer_widths[layer];
    const std::uint64_t layer_divisor = Pow3Checked(layer);
    if (n0 % layer_divisor != 0) {
      selection.notes.push_back("candidate n0 is not divisible by the current "
                                "WHIR layer domain divisor");
      return candidate;
    }
    const std::uint64_t domain_size = n0 / layer_divisor;
    const std::uint64_t shift_divisor = Pow3Checked(width);
    if (domain_size % shift_divisor != 0) {
      selection.notes.push_back("candidate n0 does not leave enough 3-adic "
                                "divisibility for a WHIR shift domain");
      return candidate;
    }

    const std::uint64_t rate_numerator = Pow3Checked(remaining_variables);
    const std::uint64_t rate_denominator = domain_size;
    if (rate_numerator >= rate_denominator) {
      selection.notes.push_back(
          "candidate leaves the WHIR unique-decoding rate regime rho_i < 1");
      return candidate;
    }
    const long double rho = static_cast<long double>(rate_numerator) /
                            static_cast<long double>(rate_denominator);
    const long double half_gap = 0.5L * (1.0L - rho);
    const long double delta = half_gap;
    if (!(delta > 0.0L) || !(delta <= half_gap)) {
      selection.notes.push_back("candidate leaves the WHIR half-gap "
                                "unique-decoding regime");
      return candidate;
    }

    const std::uint64_t repetitions =
        RepetitionCountForBits(delta, selection.repetition_security_bits);
    const long double repetition_log2 =
        static_cast<long double>(repetitions) * std::log2(1.0L - delta);
    log2_error_terms.push_back(repetition_log2);

    NTL::ZZ afold(0);
    for (std::uint64_t j = 0; j < width; ++j) {
      const std::uint64_t denominator = Pow3Checked(j + 1U);
      const std::uint64_t folded_domain = domain_size / denominator;
      afold += 2 * ToZZ(folded_domain) * ToZZ(folded_domain);
    }
    algebraic_bound += afold;
    algebraic_bound += ToZZ(width) * ToZZ(kWhirSumcheckDegreeBound);
    algebraic_bound += ToZZ(repetitions);
    algebraic_bound += 1;

    WhirUniqueDecodingLayer layer_summary;
    layer_summary.layer_index = layer;
    layer_summary.variable_count = remaining_variables;
    layer_summary.width = width;
    layer_summary.domain_size = domain_size;
    layer_summary.rate_numerator = rate_numerator;
    layer_summary.rate_denominator = rate_denominator;
    layer_summary.rate = rho;
    layer_summary.delta = delta;
    layer_summary.repetition_count = repetitions;
    layer_summary.sumcheck_degree_bound = kWhirSumcheckDegreeBound;
    layer_summary.folding_algebra_bound = ZZToString(afold);
    selection.layers.push_back(layer_summary);

    selection.public_params.layer_widths.push_back(width);
    selection.public_params.shift_repetitions.push_back(repetitions);
    selection.public_params.degree_bounds.push_back(kWhirSumcheckDegreeBound);
    selection.public_params.rates.push_back(rho);
    selection.public_params.deltas.push_back(delta);

    remaining_variables -= width;
  }

  const std::uint64_t final_domain_divisor =
      Pow3Checked(static_cast<std::uint64_t>(layer_widths.size()));
  if (n0 % final_domain_divisor != 0) {
    selection.notes.push_back(
        "candidate n0 is not divisible by the final WHIR layer domain divisor");
    return candidate;
  }
  const std::uint64_t final_domain_size = n0 / final_domain_divisor;
  if (final_domain_size <= 1) {
    selection.notes.push_back(
        "candidate final WHIR domain is too small for constant checks");
    return candidate;
  }
  const long double final_rho = 1.0L / static_cast<long double>(final_domain_size);
  const long double final_delta = 0.5L * (1.0L - final_rho);
  const std::uint64_t final_repetitions =
      RepetitionCountForBits(final_delta, selection.repetition_security_bits);
  selection.public_params.final_repetitions = final_repetitions;
  log2_error_terms.push_back(static_cast<long double>(final_repetitions) *
                             std::log2(1.0L - final_delta));

  if (algebraic_bound <= 0) {
    selection.notes.push_back("WHIR algebraic bound is unexpectedly zero");
    return candidate;
  }

  selection.rdom = multiplicative_order_mod_odd(n0, 2);
  const std::uint64_t algebraic_bits = CeilLog2ZZ(algebraic_bound);
  selection.rsec = CheckedAdd(CheckedAdd(inputs.lambda_target, 1, "WHIR rsec"),
                              algebraic_bits, "WHIR rsec");
  if (inputs.fixed_extension_degree != 0) {
    selection.selected_r = inputs.fixed_extension_degree;
  } else {
    selection.selected_r =
        CheckedMul(selection.rdom, CeilDiv(selection.rsec, selection.rdom),
                   "WHIR selected r");
  }
  selection.public_params.extension_degree = selection.selected_r;
  selection.algebraic_bound = ZZToString(algebraic_bound);
  selection.algebraic_error_log2 =
      Log2ZZ(algebraic_bound) - static_cast<long double>(selection.selected_r);
  log2_error_terms.push_back(selection.algebraic_error_log2);
  selection.total_error_log2 = Log2Sum(log2_error_terms);
  selection.effective_security_bits =
      SecurityBitsFromLog2Error(selection.total_error_log2);
  selection.feasible =
      selection.effective_security_bits >= inputs.lambda_target &&
      selection.algebraic_error_log2 <
          -static_cast<long double>(inputs.lambda_target) &&
      domain_divides_teichmuller_group(n0, selection.selected_r);

  if (!selection.feasible) {
    if (!domain_divides_teichmuller_group(n0, selection.selected_r)) {
      selection.notes.push_back(
          "candidate n0 does not divide the fixed Teichmuller group size 2^r-1");
    }
    if (!(selection.algebraic_error_log2 <
          -static_cast<long double>(inputs.lambda_target))) {
      selection.notes.push_back(
          "candidate fixed r is too small for the WHIR algebraic error target");
    }
    selection.notes.push_back(
        "candidate does not meet the WHIR unique-decoding soundness target");
  }
  candidate.valid = true;
  return candidate;
}

} // namespace

std::uint64_t multiplicative_order_mod_odd(std::uint64_t modulus,
                                           std::uint64_t base) {
  if (modulus <= 1 || (modulus & 1U) == 0U) {
    throw std::invalid_argument("multiplicative_order_mod_odd requires an odd "
                                "modulus greater than one");
  }
  if (std::gcd(base, modulus) != 1) {
    throw std::invalid_argument(
        "multiplicative_order_mod_odd requires coprime base and modulus");
  }
  std::uint64_t order = EulerPhi(modulus);
  for (const auto factor : UniquePrimeFactors(order)) {
    while (order % factor == 0 && PowMod(base, order / factor, modulus) == 1) {
      order /= factor;
    }
  }
  return order;
}

bool domain_divides_teichmuller_group(std::uint64_t domain_size,
                                      std::uint64_t extension_degree) {
  if (domain_size == 0 || (domain_size & 1U) == 0U) {
    return false;
  }
  if (domain_size == 1) {
    return true;
  }
  return PowMod(2, extension_degree, domain_size) == 1;
}

WhirUniqueDecodingSelection
select_whir_unique_decoding_parameters(const WhirUniqueDecodingInputs &inputs) {
  ValidateInputs(inputs);

  WhirUniqueDecodingSelection result;
  result.notes.push_back(
      "WHIR selector targets only the p=2 unique-decoding GR PCS mode; it does "
      "not model WHIR list-decoding or OOD uniqueness.");
  result.notes.push_back(
      "Layer domains use n_i = n0 / 3^i, matching the Phase 6 H_i.pow_map(3) "
      "oracle chain; width-b_i shift domains require n_i divisible by 3^b_i.");
  result.notes.push_back(
      "The ring exponent s is carried into public parameters; the Section 9 "
      "algebraic field-size search depends on the Teichmuller size 2^r.");
  result.notes.push_back(
      "Unique-decoding thresholds use the half-gap value "
      "delta_i=(1-rho_i)/2.");
  if (inputs.fixed_extension_degree != 0) {
    result.notes.push_back("WHIR selector uses caller-fixed extension degree r=" +
                           std::to_string(inputs.fixed_extension_degree) +
                           " and searches only compatible domains.");
  }

  const auto layer_widths =
      LayerWidths(inputs.variable_count, inputs.max_layer_width);
  const std::uint64_t required_power = RequiredThreeAdicPower(layer_widths);
  const std::uint64_t required_divisor = Pow3Checked(required_power);
  const std::uint64_t pow3_m = Pow3Checked(inputs.variable_count);
  const std::uint64_t lower_bound = LowerBoundForN0(inputs, pow3_m);
  if (lower_bound <= 1) {
    throw std::invalid_argument("WHIR n0 lower bound must exceed one");
  }

  std::uint64_t candidate =
      FirstOddMultipleAtLeast(lower_bound, required_divisor);
  const std::uint64_t step = CheckedMul(2, required_divisor, "WHIR n0 step");
  for (std::uint64_t attempt = 0; attempt < inputs.max_n0_search_steps;
       ++attempt) {
    if (inputs.max_domain_size != 0 && candidate > inputs.max_domain_size) {
      result.notes.push_back(
          "no WHIR n0 candidate fits the max_domain_size guard");
      return result;
    }

    CandidateAnalysis analysis =
        AnalyzeCandidate(inputs, layer_widths, candidate);
    if (analysis.valid) {
      if (!analysis.selection.feasible) {
        for (const auto &note : analysis.selection.notes) {
          result.notes.push_back("skipping WHIR n0 candidate " +
                                 std::to_string(candidate) + ": " + note);
        }
        result.notes.push_back("skipping a WHIR n0 candidate because its "
                               "soundness envelope is still infeasible");
      } else if (inputs.max_extension_degree == 0 ||
                 analysis.selection.selected_r <= inputs.max_extension_degree) {
        analysis.selection.notes.insert(analysis.selection.notes.begin(),
                                        result.notes.begin(), result.notes.end());
        return analysis.selection;
      } else {
        result.notes.push_back("skipping a WHIR n0 candidate because selected "
                               "r exceeds the max_extension_degree guard");
      }
    }

    if (candidate > std::numeric_limits<std::uint64_t>::max() - step) {
      result.notes.push_back("WHIR n0 search overflowed uint64_t");
      return result;
    }
    candidate += step;
  }

  result.notes.push_back("WHIR n0 search exhausted max_n0_search_steps");
  return result;
}

} // namespace swgr::whir
