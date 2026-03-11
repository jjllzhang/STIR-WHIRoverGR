#include "stir/common.hpp"

#include <NTL/ZZ_pE.h>
#include <NTL/ZZ.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <stdexcept>
#include <span>
#include <vector>

#include "algebra/teichmuller.hpp"
#include "fri/common.hpp"

using NTL::clear;
using NTL::set;

namespace swgr::stir {
namespace {

constexpr std::uint64_t kOodTagOffset = 0x10000ULL;

template <typename Sink>
void SerializeRingElement(Sink& sink, const swgr::algebra::GRContext& ctx,
                          const swgr::algebra::GRElem& value) {
  swgr::SerializeBytes(sink, ctx.serialize(value));
}

template <typename Sink>
void SerializePolynomial(Sink& sink, const swgr::algebra::GRContext& ctx,
                         const swgr::poly_utils::Polynomial& polynomial) {
  const auto& coefficients = polynomial.coefficients();
  swgr::SerializeUint64(sink,
                        static_cast<std::uint64_t>(coefficients.size()));
  for (const auto& coefficient : coefficients) {
    SerializeRingElement(sink, ctx, coefficient);
  }
}

template <typename Sink>
void SerializeMerkleProof(Sink& sink, const swgr::crypto::MerkleProof& proof) {
  swgr::SerializeUint64Vector(sink, proof.queried_indices);
  swgr::SerializeByteVector(sink, proof.leaf_payloads);
  swgr::SerializeByteVector(sink, proof.sibling_hashes);
}

template <typename Sink>
void SerializeRingVector(Sink& sink, const swgr::algebra::GRContext& ctx,
                         std::span<const swgr::algebra::GRElem> values) {
  swgr::SerializeUint64(sink, static_cast<std::uint64_t>(values.size()));
  for (const auto& value : values) {
    SerializeRingElement(sink, ctx, value);
  }
}

template <typename Sink>
void SerializeStirProofBody(Sink& sink, const swgr::algebra::GRContext& ctx,
                            const StirProof& proof) {
  swgr::SerializeBytes(sink, proof.initial_root);
  swgr::SerializeUint64(sink,
                        static_cast<std::uint64_t>(proof.rounds.size()));
  for (const auto& round : proof.rounds) {
    swgr::SerializeBytes(sink, round.g_root);
    SerializeRingVector(sink, ctx, round.betas);
    SerializePolynomial(sink, ctx, round.ans_polynomial);
    SerializeMerkleProof(sink, round.queries_to_prev);
    SerializePolynomial(sink, ctx, round.shake_polynomial);
  }
  SerializePolynomial(sink, ctx, proof.final_polynomial);
  SerializeMerkleProof(sink, proof.queries_to_final);
}

bool Contains(std::span<const algebra::GRElem> values,
              const algebra::GRElem& candidate) {
  for (const auto& value : values) {
    if (value == candidate) {
      return true;
    }
  }
  return false;
}

bool ExceptionalAgainst(const algebra::GRContext& ctx,
                        const algebra::GRElem& candidate,
                        std::span<const algebra::GRElem> others) {
  return ctx.with_ntl_context([&] {
    for (const auto& other : others) {
      if (candidate == other) {
        return false;
      }
      if (!ctx.is_unit(candidate - other)) {
        return false;
      }
    }
    return true;
  });
}

algebra::GRElem HornerEvaluate(
    std::span<const swgr::algebra::GRElem> coefficients,
    const swgr::algebra::GRElem& point) {
  swgr::algebra::GRElem acc;
  clear(acc);
  for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
    acc *= point;
    acc += *it;
  }
  return acc;
}

std::vector<swgr::algebra::GRElem> BatchHornerEvaluate(
    std::span<const swgr::algebra::GRElem> coefficients,
    std::span<const swgr::algebra::GRElem> points) {
  std::vector<swgr::algebra::GRElem> values(points.size());
  for (std::size_t point_index = 0; point_index < points.size();
       ++point_index) {
    values[point_index] = HornerEvaluate(coefficients, points[point_index]);
  }
  return values;
}

std::vector<swgr::algebra::GRElem> BatchScalingValues(
    std::span<const swgr::algebra::GRElem> points,
    const swgr::algebra::GRElem& comb_randomness, std::uint64_t gap) {
  std::vector<swgr::algebra::GRElem> out(points.size());
  for (std::size_t point_index = 0; point_index < points.size();
       ++point_index) {
    const auto common_factor = points[point_index] * comb_randomness;
    swgr::algebra::GRElem term;
    set(term);
    swgr::algebra::GRElem total;
    clear(total);
    for (std::uint64_t exponent = 0; exponent <= gap; ++exponent) {
      total += term;
      term *= common_factor;
    }
    out[point_index] = total;
  }
  return out;
}

std::vector<swgr::algebra::GRElem> EnumerateDomainPoints(const Domain& domain) {
  std::vector<swgr::algebra::GRElem> points;
  points.reserve(static_cast<std::size_t>(domain.size()));

  auto current = domain.offset();
  for (std::uint64_t index = 0; index < domain.size(); ++index) {
    points.push_back(current);
    current *= domain.root();
  }
  return points;
}

constexpr std::uint64_t kTheoremSamplerAttemptFactor = 64U;

bool TheoremSafeComplementCandidate(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    std::span<const swgr::algebra::GRElem> excluded_points,
    std::span<const swgr::algebra::GRElem> chosen_points,
    const swgr::algebra::GRElem& candidate) {
  const auto& ctx = input_domain.context();
  const auto input_points = input_domain.elements();
  const auto shift_points = shift_domain.elements();
  const auto folded_points = folded_domain.elements();

  if (candidate == ctx.zero() || Contains(input_points, candidate) ||
      Contains(shift_points, candidate) || Contains(folded_points, candidate) ||
      Contains(excluded_points, candidate) || Contains(chosen_points, candidate)) {
    return false;
  }

  return ExceptionalAgainst(ctx, candidate, input_points) &&
         ExceptionalAgainst(ctx, candidate, shift_points) &&
         ExceptionalAgainst(ctx, candidate, folded_points) &&
         ExceptionalAgainst(ctx, candidate, excluded_points) &&
         ExceptionalAgainst(ctx, candidate, chosen_points);
}

std::vector<swgr::algebra::GRElem> SampleTheoremSafeComplement(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    std::span<const swgr::algebra::GRElem> excluded_points,
    swgr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t sample_count) {
  if (sample_count == 0) {
    return {};
  }

  std::vector<swgr::algebra::GRElem> result;
  result.reserve(static_cast<std::size_t>(sample_count));
  const auto teich_size = swgr::algebra::teichmuller_set_size(input_domain.context());
  if (teich_size <= 1) {
    throw std::runtime_error("theorem sampler requires a non-empty T*");
  }

  const std::uint64_t max_attempts =
      std::max<std::uint64_t>(sample_count * kTheoremSamplerAttemptFactor,
                              kTheoremSamplerAttemptFactor);
  for (std::uint64_t attempt = 0;
       result.size() < static_cast<std::size_t>(sample_count) &&
       attempt < max_attempts;
       ++attempt) {
    const auto candidate = transcript.challenge_teichmuller(
        input_domain.context(),
        std::string(label_prefix) + ":" + std::to_string(attempt));
    if (!TheoremSafeComplementCandidate(input_domain, shift_domain,
                                        folded_domain, excluded_points, result,
                                        candidate)) {
      continue;
    }
    result.push_back(candidate);
  }

  if (result.size() != static_cast<std::size_t>(sample_count)) {
    throw std::runtime_error("theorem sampler failed to find enough samples");
  }
  return result;
}

}  // namespace

std::uint64_t serialized_message_bytes(const swgr::algebra::GRContext& ctx,
                                       const StirProof& proof) {
  swgr::CountingSink sink;
  SerializeStirProofBody(sink, ctx, proof);
  return sink.size();
}

bool points_have_unit_differences(
    const Domain& domain, std::span<const swgr::algebra::GRElem> points) {
  const auto& ctx = domain.context();
  const auto domain_points = domain.elements();
  return ctx.with_ntl_context([&] {
    for (const auto& point : points) {
      for (const auto& domain_point : domain_points) {
        if (!ctx.is_unit(point - domain_point)) {
          return false;
        }
      }
    }
    return true;
  });
}

bool domains_have_unit_differences(const Domain& lhs, const Domain& rhs) {
  const auto& lhs_cfg = lhs.context().config();
  const auto& rhs_cfg = rhs.context().config();
  if (lhs_cfg.p != rhs_cfg.p || lhs_cfg.k_exp != rhs_cfg.k_exp ||
      lhs_cfg.r != rhs_cfg.r) {
    throw std::invalid_argument(
        "domains_have_unit_differences requires the same ring");
  }

  const auto rhs_points = rhs.elements();
  return points_have_unit_differences(lhs, rhs_points);
}

swgr::algebra::GRElem derive_stir_folding_challenge(
    swgr::crypto::Transcript& transcript, const swgr::algebra::GRContext& ctx,
    std::string_view label) {
  return transcript.challenge_teichmuller(ctx, label);
}

swgr::algebra::GRElem derive_stir_comb_challenge(
    swgr::crypto::Transcript& transcript, const swgr::algebra::GRContext& ctx,
    std::string_view label) {
  return transcript.challenge_teichmuller(ctx, label);
}

bool domain_is_subset_of_teichmuller_units(const Domain& domain) {
  const auto& ctx = domain.context();
  if (!domain.is_teichmuller_subset()) {
    return false;
  }
  return ctx.with_ntl_context([&] {
    for (const auto& point : domain.elements()) {
      if (point == ctx.zero() || !ctx.is_unit(point)) {
        return false;
      }
    }
    return true;
  });
}

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

bool theorem_ood_pool_has_capacity(const Domain& input_domain,
                                   const Domain& shift_domain,
                                   const Domain& folded_domain,
                                   std::uint64_t required_points) {
  if (!domain_is_subset_of_teichmuller_units(input_domain) ||
      !domain_is_subset_of_teichmuller_units(shift_domain) ||
      !domain_is_subset_of_teichmuller_units(folded_domain)) {
    return false;
  }
  if (required_points == 0) {
    return true;
  }

  const auto teich_size = swgr::algebra::teichmuller_set_size(input_domain.context());
  if (teich_size <= 1) {
    return false;
  }

  const auto input_points = input_domain.elements();
  const auto shift_points = shift_domain.elements();
  const auto folded_points = folded_domain.elements();
  std::vector<swgr::algebra::GRElem> excluded_points;
  excluded_points.reserve(input_points.size() + shift_points.size() +
                          folded_points.size());
  for (const auto& point : input_points) {
    if (!Contains(excluded_points, point)) {
      excluded_points.push_back(point);
    }
  }
  for (const auto& point : shift_points) {
    if (!Contains(excluded_points, point)) {
      excluded_points.push_back(point);
    }
  }
  for (const auto& point : folded_points) {
    if (!Contains(excluded_points, point)) {
      excluded_points.push_back(point);
    }
  }

  const auto available = teich_size - NTL::ZZ(1) -
                         ToZZ(static_cast<std::uint64_t>(excluded_points.size()),
                              "excluded theorem points");
  return available >= ToZZ(required_points, "required theorem points");
}

bool theorem_shake_pool_has_capacity(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    std::span<const swgr::algebra::GRElem> quotient_points) {
  const auto& ctx = input_domain.context();
  if (!domain_is_subset_of_teichmuller_units(input_domain) ||
      !domain_is_subset_of_teichmuller_units(shift_domain) ||
      !domain_is_subset_of_teichmuller_units(folded_domain)) {
    return false;
  }
  for (const auto& point : quotient_points) {
    if (!swgr::algebra::is_teichmuller_element(ctx, point) ||
        !ctx.is_unit(point)) {
      return false;
    }
  }

  const auto input_points = input_domain.elements();
  const auto shift_points = shift_domain.elements();
  const auto folded_points = folded_domain.elements();
  std::vector<swgr::algebra::GRElem> excluded_points;
  excluded_points.reserve(input_points.size() + shift_points.size() +
                          folded_points.size() + quotient_points.size());
  for (const auto& point : input_points) {
    if (!Contains(excluded_points, point)) {
      excluded_points.push_back(point);
    }
  }
  for (const auto& point : shift_points) {
    if (!Contains(excluded_points, point)) {
      excluded_points.push_back(point);
    }
  }
  for (const auto& point : folded_points) {
    if (!Contains(excluded_points, point)) {
      excluded_points.push_back(point);
    }
  }
  for (const auto& point : quotient_points) {
    if (!Contains(excluded_points, point)) {
      excluded_points.push_back(point);
    }
  }

  const auto teich_size = swgr::algebra::teichmuller_set_size(ctx);
  const auto available = teich_size - NTL::ZZ(1) -
                         ToZZ(static_cast<std::uint64_t>(excluded_points.size()),
                              "excluded theorem points");
  return available >= NTL::ZZ(1);
}

std::uint64_t folded_degree_bound(std::uint64_t degree_bound,
                                  std::uint64_t fold_factor) {
  if (fold_factor < 2) {
    throw std::invalid_argument("folded_degree_bound requires fold_factor >= 2");
  }
  if (degree_bound == 0) {
    return 0;
  }
  return ((degree_bound + 1U) + fold_factor - 1U) / fold_factor - 1U;
}

std::size_t folding_round_count(const StirInstance& instance,
                                const StirParameters& params) {
  std::size_t rounds = 0;
  std::uint64_t current_domain_size = instance.domain.size();
  std::uint64_t current_degree_bound = instance.claimed_degree;
  while (current_degree_bound > params.stop_degree) {
    if (current_domain_size % params.virtual_fold_factor != 0 ||
        current_domain_size % params.shift_power != 0) {
      throw std::invalid_argument(
          "folding_round_count requires divisible STIR domain chain");
    }
    current_domain_size /= params.shift_power;
    current_degree_bound =
        folded_degree_bound(current_degree_bound, params.virtual_fold_factor);
    ++rounds;
  }
  return rounds;
}

std::vector<std::uint64_t> derive_unique_positions(
    const std::vector<std::uint8_t>& seed_material, std::uint64_t round_tag,
    std::uint64_t modulus, std::uint64_t requested_count) {
  if (modulus == 0 || requested_count == 0) {
    return {};
  }

  const std::uint64_t capped_count = std::min(requested_count, modulus);
  auto positions =
      swgr::fri::derive_query_positions(seed_material, round_tag, modulus,
                                        capped_count);
  std::vector<std::uint64_t> unique_positions;
  unique_positions.reserve(static_cast<std::size_t>(capped_count));
  for (const auto position : positions) {
    if (std::find(unique_positions.begin(), unique_positions.end(), position) ==
        unique_positions.end()) {
      unique_positions.push_back(position);
    }
  }
  for (std::uint64_t fallback = 0;
       unique_positions.size() < static_cast<std::size_t>(capped_count) &&
       fallback < modulus;
       ++fallback) {
    if (std::find(unique_positions.begin(), unique_positions.end(), fallback) ==
        unique_positions.end()) {
      unique_positions.push_back(fallback);
    }
  }
  return unique_positions;
}

std::vector<std::uint64_t> derive_unique_positions(
    swgr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t modulus, std::uint64_t requested_count) {
  return swgr::fri::derive_unique_query_positions(
      transcript, label_prefix, modulus, requested_count);
}

std::vector<swgr::algebra::GRElem> derive_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    const std::vector<std::uint8_t>& oracle_commitment, std::uint64_t round_tag,
    std::uint64_t sample_count) {
  if (sample_count == 0) {
    return {};
  }

  const Domain candidate_domain = input_domain.scale_offset(1);
  const auto shift_points = shift_domain.elements();
  const auto folded_points = folded_domain.elements();
  const auto seed_positions =
      derive_unique_positions(oracle_commitment, round_tag + kOodTagOffset,
                              candidate_domain.size(), candidate_domain.size());
  const auto& ctx = candidate_domain.context();

  std::vector<swgr::algebra::GRElem> result;
  result.reserve(static_cast<std::size_t>(sample_count));
  for (const auto index : seed_positions) {
    const auto candidate = candidate_domain.element(index);
    if (Contains(shift_points, candidate) || Contains(folded_points, candidate) ||
        Contains(result, candidate)) {
      continue;
    }
    if (!ExceptionalAgainst(ctx, candidate, shift_points) ||
        !ExceptionalAgainst(ctx, candidate, folded_points) ||
        !ExceptionalAgainst(ctx, candidate, result)) {
      continue;
    }
    result.push_back(candidate);
    if (result.size() == static_cast<std::size_t>(sample_count)) {
      return result;
    }
  }

  throw std::runtime_error("derive_ood_points failed to find enough samples");
}

std::vector<swgr::algebra::GRElem> derive_ood_points(
    const StirParameters& params, const Domain& input_domain,
    const Domain& shift_domain, const Domain& folded_domain,
    swgr::crypto::Transcript& transcript, std::string_view label_prefix,
    std::uint64_t sample_count) {
  if (params.protocol_mode == StirProtocolMode::TheoremGr) {
    return derive_theorem_ood_points(input_domain, shift_domain, folded_domain,
                                     transcript, label_prefix, sample_count);
  }
  return derive_ood_points(input_domain, shift_domain, folded_domain, transcript,
                           label_prefix, sample_count);
}

std::vector<swgr::algebra::GRElem> derive_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain, swgr::crypto::Transcript& transcript,
    std::string_view label_prefix, std::uint64_t sample_count) {
  if (sample_count == 0) {
    return {};
  }

  const Domain candidate_domain = input_domain.scale_offset(1);
  const auto shift_points = shift_domain.elements();
  const auto folded_points = folded_domain.elements();
  const auto& ctx = candidate_domain.context();

  std::vector<swgr::algebra::GRElem> result;
  result.reserve(static_cast<std::size_t>(sample_count));
  for (std::uint64_t attempt = 0;
       result.size() < static_cast<std::size_t>(sample_count) &&
       attempt < candidate_domain.size() * 4;
       ++attempt) {
    const auto index = transcript.challenge_index(
        std::string(label_prefix) + ":" + std::to_string(attempt),
        candidate_domain.size());
    const auto candidate = candidate_domain.element(index);
    if (Contains(shift_points, candidate) || Contains(folded_points, candidate) ||
        Contains(result, candidate)) {
      continue;
    }
    if (!ExceptionalAgainst(ctx, candidate, shift_points) ||
        !ExceptionalAgainst(ctx, candidate, folded_points) ||
        !ExceptionalAgainst(ctx, candidate, result)) {
      continue;
    }
    result.push_back(candidate);
  }

  if (result.size() != static_cast<std::size_t>(sample_count)) {
    throw std::runtime_error("derive_ood_points failed to find enough samples");
  }
  return result;
}

std::vector<swgr::algebra::GRElem> derive_theorem_ood_points(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain, swgr::crypto::Transcript& transcript,
    std::string_view label_prefix, std::uint64_t sample_count) {
  return SampleTheoremSafeComplement(
      input_domain, shift_domain, folded_domain,
      std::span<const swgr::algebra::GRElem>(), transcript, label_prefix,
      sample_count);
}

swgr::algebra::GRElem derive_shake_point(
    const StirParameters& params, const Domain& input_domain,
    const Domain& shift_domain, const Domain& folded_domain,
    const std::vector<swgr::algebra::GRElem>& quotient_points,
    swgr::crypto::Transcript& transcript, std::string_view label_prefix) {
  if (params.protocol_mode == StirProtocolMode::TheoremGr) {
    return derive_theorem_shake_point(input_domain, shift_domain, folded_domain,
                                      quotient_points, transcript,
                                      label_prefix);
  }
  return derive_shake_point(input_domain, shift_domain, folded_domain,
                            quotient_points, transcript, label_prefix);
}

swgr::algebra::GRElem derive_shake_point(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    const std::vector<swgr::algebra::GRElem>& quotient_points,
    swgr::crypto::Transcript& transcript, std::string_view label_prefix) {
  const Domain candidate_domain = input_domain.scale_offset(1);
  const auto shift_points = shift_domain.elements();
  const auto folded_points = folded_domain.elements();
  const auto& ctx = candidate_domain.context();

  for (std::uint64_t attempt = 0; attempt < candidate_domain.size() * 4;
       ++attempt) {
    const auto index = transcript.challenge_index(
        std::string(label_prefix) + ":" + std::to_string(attempt),
        candidate_domain.size());
    const auto candidate = candidate_domain.element(index);
    if (Contains(shift_points, candidate) || Contains(folded_points, candidate) ||
        Contains(quotient_points, candidate)) {
      continue;
    }
    if (!ExceptionalAgainst(ctx, candidate, quotient_points)) {
      continue;
    }
    return candidate;
  }

  throw std::runtime_error("derive_shake_point failed to find a valid sample");
}

swgr::algebra::GRElem derive_theorem_shake_point(
    const Domain& input_domain, const Domain& shift_domain,
    const Domain& folded_domain,
    const std::vector<swgr::algebra::GRElem>& quotient_points,
    swgr::crypto::Transcript& transcript, std::string_view label_prefix) {
  return SampleTheoremSafeComplement(input_domain, shift_domain, folded_domain,
                                     quotient_points, transcript, label_prefix,
                                     1)
      .front();
}

bool try_reuse_next_round_input_oracle(
    const Domain& domain,
    const std::vector<swgr::algebra::GRElem>& shifted_oracle_evals,
    const swgr::poly_utils::Polynomial& answer_polynomial,
    const swgr::poly_utils::Polynomial& vanishing_polynomial,
    const swgr::poly_utils::Polynomial& quotient_polynomial,
    const swgr::algebra::GRElem& comb_randomness,
    std::uint64_t target_degree_bound, std::uint64_t current_degree_bound,
    std::vector<swgr::algebra::GRElem>* next_oracle_evals) {
  if (next_oracle_evals == nullptr) {
    throw std::invalid_argument(
        "try_reuse_next_round_input_oracle requires output storage");
  }
  if (shifted_oracle_evals.size() != domain.size()) {
    throw std::invalid_argument(
        "try_reuse_next_round_input_oracle requires eval count == domain size");
  }
  if (current_degree_bound > target_degree_bound) {
    throw std::invalid_argument(
        "try_reuse_next_round_input_oracle requires current <= target degree");
  }

  const auto& ctx = domain.context();
  return ctx.with_ntl_context([&] {
    const auto points = EnumerateDomainPoints(domain);
    const auto answer_values =
        BatchHornerEvaluate(answer_polynomial.coefficients(), points);
    const auto vanishing_values =
        BatchHornerEvaluate(vanishing_polynomial.coefficients(), points);
    const std::uint64_t gap = target_degree_bound - current_degree_bound;
    const auto scaling_values =
        BatchScalingValues(points, comb_randomness, gap);

    std::vector<swgr::algebra::GRElem> invertible_denominators;
    invertible_denominators.reserve(points.size());
    for (std::size_t point_index = 0; point_index < points.size();
         ++point_index) {
      if (vanishing_values[point_index] == 0) {
        continue;
      }
      if (!ctx.is_unit(vanishing_values[point_index])) {
        return false;
      }
      invertible_denominators.push_back(vanishing_values[point_index]);
    }

    std::vector<swgr::algebra::GRElem> denominator_inverses;
    if (!invertible_denominators.empty()) {
      try {
        denominator_inverses = ctx.batch_inv(invertible_denominators);
      } catch (const std::invalid_argument&) {
        return false;
      }
    }

    next_oracle_evals->resize(points.size());
    std::size_t denominator_index = 0;
    for (std::size_t point_index = 0; point_index < points.size();
         ++point_index) {
      swgr::algebra::GRElem quotient_value;
      if (vanishing_values[point_index] == 0) {
        quotient_value = HornerEvaluate(
            quotient_polynomial.coefficients(), points[point_index]);
      } else {
        quotient_value =
            (shifted_oracle_evals[point_index] - answer_values[point_index]) *
            denominator_inverses[denominator_index++];
      }
      (*next_oracle_evals)[point_index] =
          quotient_value * scaling_values[point_index];
    }
    return true;
  });
}

}  // namespace swgr::stir
