#include "algebra/teichmuller.hpp"

#include <NTL/ZZ.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include <NTL/ZZ_pE.h>

using NTL::ZZ;
using NTL::bit;
using NTL::NumBits;
using NTL::power;
using NTL::set;

namespace stir_whir_gr::algebra {
namespace {

constexpr std::uint64_t kFallbackSeed = 0xC0DEC0FFEE12345ULL;

long CheckedLong(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument("subgroup size exceeds long");
  }
  return static_cast<long>(value);
}

std::uint64_t SplitMix64(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

std::vector<std::uint8_t> DeterministicCandidateBytes(const GRContext& ctx,
                                                      std::uint64_t size,
                                                      std::uint64_t attempt) {
  std::vector<std::uint8_t> bytes(ctx.elem_bytes(), 0);
  std::uint64_t state =
      kFallbackSeed ^ (ctx.config().p << 48U) ^ (ctx.config().k_exp << 24U) ^
      ctx.config().r ^ (size << 7U) ^ attempt;
  for (std::size_t offset = 0; offset < bytes.size(); offset += sizeof(std::uint64_t)) {
    const auto word = SplitMix64(state);
    const std::size_t chunk =
        std::min<std::size_t>(sizeof(std::uint64_t), bytes.size() - offset);
    for (std::size_t i = 0; i < chunk; ++i) {
      bytes[offset + i] =
          static_cast<std::uint8_t>((word >> (8U * i)) & 0xFFU);
    }
  }
  return bytes;
}

std::set<std::uint64_t> PrimeDivisors(std::uint64_t value) {
  std::set<std::uint64_t> divisors;
  for (std::uint64_t factor = 2; factor * factor <= value; ++factor) {
    if (value % factor != 0) {
      continue;
    }
    divisors.insert(factor);
    while (value % factor == 0) {
      value /= factor;
    }
  }
  if (value > 1) {
    divisors.insert(value);
  }
  return divisors;
}

bool element_has_exact_order(const GRContext& ctx, const GRElem& x,
                             std::uint64_t order) {
  if (order == 0) {
    return false;
  }

  return ctx.with_ntl_context([&] {
    if (power(x, CheckedLong(order)) != ctx.one()) {
      return false;
    }

    for (const auto prime_divisor : PrimeDivisors(order)) {
      if (power(x, CheckedLong(order / prime_divisor)) == ctx.one()) {
        return false;
      }
    }
    return true;
  });
}

GRElem PowerByZZ(const GRElem& base, const ZZ& exponent) {
  if (exponent < 0) {
    throw std::invalid_argument("PowerByZZ requires non-negative exponent");
  }

  GRElem result;
  set(result);
  GRElem current = base;
  const long bit_count = NumBits(exponent);
  for (long bit_index = 0; bit_index < bit_count; ++bit_index) {
    if (bit(exponent, bit_index) != 0) {
      result *= current;
    }
    if (bit_index + 1 < bit_count) {
      current *= current;
    }
  }
  return result;
}

}  // namespace

GRElem teichmuller_generator(const GRContext& ctx) { return ctx.teich_generator(); }

ZZ teichmuller_group_order(const GRContext& ctx) {
  return power(ctx.prime(), CheckedLong(ctx.config().r)) - ZZ(1);
}

ZZ teichmuller_set_size(const GRContext& ctx) {
  return teichmuller_group_order(ctx) + ZZ(1);
}

bool teichmuller_subgroup_size_supported(const GRContext& ctx,
                                         std::uint64_t size) {
  if (size == 0) {
    return false;
  }

  const ZZ order = teichmuller_group_order(ctx);
  return order % ZZ(size) == 0;
}

bool is_teichmuller_element(const GRContext& ctx, const GRElem& element) {
  return ctx.with_ntl_context([&] {
    if (element == ctx.zero()) {
      return true;
    }
    if (!ctx.is_unit(element)) {
      return false;
    }

    return static_cast<bool>(
        power(element, teichmuller_group_order(ctx)) == ctx.one());
  });
}

GRElem teichmuller_element_by_index(const GRContext& ctx, const ZZ& index) {
  const ZZ size = teichmuller_set_size(ctx);
  if (index < 0 || index >= size) {
    throw std::invalid_argument(
        "teichmuller_element_by_index requires index in [0, |T|-1]");
  }

  const GRElem generator = teichmuller_generator(ctx);
  return ctx.with_ntl_context([&] {
    if (index == 0) {
      return ctx.zero();
    }
    return PowerByZZ(generator, index - ZZ(1));
  });
}

GRElem teichmuller_subgroup_generator(const GRContext& ctx,
                                      std::uint64_t size) {
  if (!teichmuller_subgroup_size_supported(ctx, size)) {
    throw std::invalid_argument(
        "teichmuller_subgroup_generator requires size dividing p^r - 1");
  }

  return ctx.with_ntl_context([&] {
    const ZZ teich_order = teichmuller_group_order(ctx);
    const ZZ subgroup_exponent = teich_order / ZZ(size);
    const ZZ projection_exponent =
        power(ctx.prime(), CheckedLong(ctx.config().k_exp - 1));

    const auto try_candidate = [&](const GRElem& base,
                                   bool already_projected) -> std::optional<GRElem> {
      const GRElem projected =
          already_projected ? base : power(base, projection_exponent);
      const GRElem candidate = power(projected, subgroup_exponent);
      if (element_has_exact_order(ctx, candidate, size)) {
        return candidate;
      }
      return std::nullopt;
    };

    if (const auto from_teich = try_candidate(ctx.teich_generator(), true);
        from_teich.has_value()) {
      return *from_teich;
    }

    for (std::uint64_t attempt = 0; attempt < 2048; ++attempt) {
      const auto bytes = DeterministicCandidateBytes(ctx, size, attempt);
      const GRElem base = ctx.deserialize(bytes);
      if (base == ctx.zero()) {
        continue;
      }
      if (const auto candidate = try_candidate(base, false);
          candidate.has_value()) {
        return *candidate;
      }
    }

    throw std::runtime_error(
        "teichmuller_subgroup_generator produced incorrect order");
  });
}

bool has_exact_multiplicative_order(const GRContext& ctx, const GRElem& x,
                                    std::uint64_t order) {
  return element_has_exact_order(ctx, x, order);
}

std::vector<GRElem> generate_teichmuller_subgroup(const GRContext& ctx,
                                                  std::uint64_t size) {
  if (size == 0) {
    throw std::invalid_argument("generate_teichmuller_subgroup requires size > 0");
  }

  return ctx.with_ntl_context([&] {
    std::vector<GRElem> out;
    out.reserve(static_cast<std::size_t>(size));

    GRElem current;
    set(current);
    const GRElem generator = teichmuller_subgroup_generator(ctx, size);
    const long size_long = CheckedLong(size);
    for (long i = 0; i < size_long; ++i) {
      out.push_back(current);
      current *= generator;
    }
    return out;
  });
}

}  // namespace stir_whir_gr::algebra
