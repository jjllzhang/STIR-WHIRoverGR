#include "algebra/teichmuller.hpp"

#include <NTL/ZZ.h>
#include <limits>
#include <set>
#include <stdexcept>

#include <NTL/ZZ_pE.h>

using NTL::ZZ;
using NTL::power;
using NTL::set;

namespace swgr::algebra {
namespace {

long CheckedLong(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument("subgroup size exceeds long");
  }
  return static_cast<long>(value);
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

}  // namespace

GRElem teichmuller_generator(const GRContext& ctx) { return ctx.teich_generator(); }

ZZ teichmuller_group_order(const GRContext& ctx) {
  return power(ctx.prime(), CheckedLong(ctx.config().r)) - ZZ(1);
}

bool teichmuller_subgroup_size_supported(const GRContext& ctx,
                                         std::uint64_t size) {
  if (size == 0) {
    return false;
  }

  const ZZ order = teichmuller_group_order(ctx);
  return order % ZZ(size) == 0;
}

GRElem teichmuller_subgroup_generator(const GRContext& ctx,
                                      std::uint64_t size) {
  if (!teichmuller_subgroup_size_supported(ctx, size)) {
    throw std::invalid_argument(
        "teichmuller_subgroup_generator requires size dividing p^r - 1");
  }

  return ctx.with_ntl_context([&] {
    const ZZ exponent = teichmuller_group_order(ctx) / ZZ(size);
    const GRElem generator = power(ctx.teich_generator(), exponent);
    if (!element_has_exact_order(ctx, generator, size)) {
      throw std::runtime_error(
          "teichmuller_subgroup_generator produced incorrect order");
    }
    return generator;
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

}  // namespace swgr::algebra
