#include "stir/common.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>

#include "fri/common.hpp"

namespace swgr::stir {
namespace {

constexpr std::uint64_t kOodTagOffset = 0x10000ULL;

bool Contains(const std::vector<algebra::GRElem>& values,
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
                        const std::vector<algebra::GRElem>& others) {
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

}  // namespace

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
    if (!ExceptionalAgainst(ctx, candidate, folded_points) ||
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
    if (!ExceptionalAgainst(ctx, candidate, folded_points) ||
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

}  // namespace swgr::stir
