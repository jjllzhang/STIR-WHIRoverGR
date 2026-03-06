#include "algebra/subgroup.hpp"

#include <limits>
#include <stdexcept>

#include <NTL/ZZ_pE.h>

using NTL::set;

namespace swgr::algebra {
namespace {

long CheckedLong(std::uint64_t value) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument("subgroup size exceeds long");
  }
  return static_cast<long>(value);
}

}  // namespace

std::vector<GRElem> enumerate_cyclic_subgroup(const GRContext& ctx,
                                              const GRElem& root,
                                              std::uint64_t size);

std::vector<GRElem> generate_cyclic_subgroup(const GRContext& ctx,
                                              const GRElem& root,
                                              std::uint64_t size) {
  if (size == 0) {
    throw std::invalid_argument("generate_cyclic_subgroup requires size > 0");
  }

  return ctx.with_ntl_context([&] {
    std::vector<GRElem> out;
    out.reserve(static_cast<std::size_t>(size));

    GRElem current;
    set(current);
    const long size_long = CheckedLong(size);
    for (long i = 0; i < size_long; ++i) {
      out.push_back(current);
      current *= root;
    }
    return out;
  });
}

std::vector<GRElem> enumerate_cyclic_subgroup(const GRContext& ctx,
                                              const GRElem& root,
                                              std::uint64_t size) {
  return generate_cyclic_subgroup(ctx, root, size);
}

}  // namespace swgr::algebra
