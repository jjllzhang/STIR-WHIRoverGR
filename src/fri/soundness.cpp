#include "fri/soundness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <stdexcept>

#include <NTL/ZZ.h>

namespace swgr::fri {
namespace {

long CheckedLong(std::uint64_t value, const char* label) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

NTL::ZZ ToZZ(std::uint64_t value, const char* label) {
  return NTL::ZZ(CheckedLong(value, label));
}

NTL::ZZ PowZZ(NTL::ZZ base, std::uint64_t exponent) {
  NTL::ZZ result(1);
  while (exponent != 0) {
    if ((exponent & 1U) != 0U) {
      result *= base;
    }
    exponent >>= 1U;
    if (exponent != 0) {
      base *= base;
    }
  }
  return result;
}

bool RepetitionTermWithinTarget(std::uint64_t domain_size,
                                std::uint64_t agreement_radius,
                                std::uint64_t lambda_target,
                                std::uint64_t repetition_count) {
  const NTL::ZZ numerator =
      PowZZ(ToZZ(domain_size - agreement_radius, "domain_size"), repetition_count);
  const NTL::ZZ denominator =
      PowZZ(ToZZ(domain_size, "domain_size"), repetition_count);
  const NTL::ZZ target = PowZZ(NTL::ZZ(2), lambda_target);
  return numerator * target <= denominator;
}

std::uint64_t InitialRepetitionGuess(std::uint64_t domain_size,
                                     std::uint64_t agreement_radius,
                                     std::uint64_t lambda_target) {
  const long double delta =
      static_cast<long double>(agreement_radius) /
      static_cast<long double>(domain_size);
  const long double log_one_minus_delta = std::log1p(-delta);
  if (!std::isfinite(log_one_minus_delta) || log_one_minus_delta >= 0.0L) {
    throw std::invalid_argument(
        "standalone FRI PCS theorem auto-parameterization produced an "
        "invalid logarithmic repetition estimate");
  }

  const long double numerator =
      static_cast<long double>(lambda_target) * std::log(2.0L);
  const long double estimate = std::ceil(numerator / (-log_one_minus_delta));
  if (!std::isfinite(estimate) || estimate < 1.0L ||
      estimate >
          static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
    throw std::overflow_error(
        "standalone FRI PCS logarithmic repetition estimate exceeds uint64_t");
  }
  return static_cast<std::uint64_t>(estimate);
}

}  // namespace

StandaloneFriSoundnessAnalysis analyze_standalone_soundness(
    const StandaloneFriSoundnessInputs& inputs) {
  if (inputs.base_prime != 2) {
    throw std::invalid_argument(
        "standalone FRI PCS theorem auto-parameterization currently requires p=2");
  }
  if (inputs.ring_extension_degree == 0 || inputs.domain_size == 0 ||
      inputs.fold_factor == 0 || inputs.quotient_code_dimension == 0 ||
      inputs.lambda_target == 0) {
    throw std::invalid_argument(
        "standalone FRI PCS soundness inputs must all be non-zero");
  }
  if (inputs.quotient_code_dimension >= inputs.domain_size) {
    throw std::invalid_argument(
        "standalone FRI PCS requires quotient_code_dimension < domain_size");
  }

  const std::uint64_t agreement_radius =
      (inputs.domain_size - inputs.quotient_code_dimension) / 2U;
  if (agreement_radius == 0) {
    throw std::invalid_argument(
        "standalone FRI PCS theorem auto-parameterization has delta=0, so no "
        "finite repetition count m can satisfy a positive lambda target");
  }

  const std::uint64_t delta_gcd =
      std::gcd(agreement_radius, inputs.domain_size);
  StandaloneFriSoundnessAnalysis result;
  result.agreement_radius = agreement_radius;
  result.delta_numerator = agreement_radius / delta_gcd;
  result.delta_denominator = inputs.domain_size / delta_gcd;

  const NTL::ZZ span_numerator =
      ToZZ(inputs.fold_factor, "fold_factor") * ToZZ(inputs.domain_size, "domain_size");
  const NTL::ZZ span_denominator =
      PowZZ(NTL::ZZ(2), inputs.ring_extension_degree);
  const NTL::ZZ span_target = PowZZ(NTL::ZZ(2), inputs.lambda_target);
  result.span_term_within_target =
      span_numerator * span_target <= span_denominator;

  std::uint64_t repetition_count = InitialRepetitionGuess(
      inputs.domain_size, agreement_radius, inputs.lambda_target);

  while (!RepetitionTermWithinTarget(inputs.domain_size, agreement_radius,
                                     inputs.lambda_target, repetition_count)) {
    if (repetition_count == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error(
          "standalone FRI PCS minimum repetition count exceeds uint64_t");
    }
    ++repetition_count;
  }
  while (repetition_count > 1U &&
         RepetitionTermWithinTarget(inputs.domain_size, agreement_radius,
                                    inputs.lambda_target,
                                    repetition_count - 1U)) {
    --repetition_count;
  }

  result.minimum_repetition_count = repetition_count;
  return result;
}

}  // namespace swgr::fri
