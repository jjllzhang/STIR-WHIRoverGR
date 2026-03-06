#include "domain.hpp"

#include <NTL/ZZ_pE.h>

#include "algebra/teichmuller.hpp"

#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using NTL::power;

namespace swgr {
namespace {

long CheckedLong(std::uint64_t value, const char* label) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

}  // namespace

Domain Domain::from_generator(const algebra::GRContext& ctx,
                              algebra::GRElem offset, algebra::GRElem root,
                              std::uint64_t size) {
  return Domain(ctx, std::move(offset), std::move(root), size);
}

Domain Domain::from_generator(std::shared_ptr<const algebra::GRContext> ctx,
                              algebra::GRElem offset, algebra::GRElem root,
                              std::uint64_t size) {
  return Domain(std::move(ctx), std::move(offset), std::move(root), size);
}

Domain Domain::teichmuller_subgroup(const algebra::GRContext& ctx,
                                    std::uint64_t size) {
  return teichmuller_subgroup(std::make_shared<algebra::GRContext>(ctx), size);
}

Domain Domain::teichmuller_subgroup(
    std::shared_ptr<const algebra::GRContext> ctx, std::uint64_t size) {
  const auto root = algebra::teichmuller_subgroup_generator(*ctx, size);
  return Domain(std::move(ctx), ctx->one(), std::move(root), size);
}

Domain Domain::teichmuller_coset(const algebra::GRContext& ctx,
                                 algebra::GRElem offset,
                                 std::uint64_t size) {
  return teichmuller_coset(std::make_shared<algebra::GRContext>(ctx),
                           std::move(offset), size);
}

Domain Domain::teichmuller_coset(
    std::shared_ptr<const algebra::GRContext> ctx, algebra::GRElem offset,
    std::uint64_t size) {
  const auto root = algebra::teichmuller_subgroup_generator(*ctx, size);
  return Domain(std::move(ctx), std::move(offset), std::move(root), size);
}

Domain::Domain(const algebra::GRContext& ctx, algebra::GRElem offset,
               algebra::GRElem root, std::uint64_t size)
    : Domain(std::make_shared<algebra::GRContext>(ctx), std::move(offset),
             std::move(root), size) {}

Domain::Domain(std::shared_ptr<const algebra::GRContext> ctx,
               algebra::GRElem offset, algebra::GRElem root, std::uint64_t size)
    : ctx_(std::move(ctx)),
      offset_(std::move(offset)),
      root_(std::move(root)),
      size_(size) {
  if (!ctx_) {
    throw std::invalid_argument("Domain requires a valid GRContext");
  }
  if (size_ == 0) {
    throw std::invalid_argument("Domain requires size > 0");
  }
  if (!ctx_->is_unit(offset_)) {
    throw std::invalid_argument("Domain requires unit offset");
  }
  if (!ctx_->is_unit(root_)) {
    throw std::invalid_argument("Domain requires unit root");
  }
  if (!algebra::has_exact_multiplicative_order(*ctx_, root_, size_)) {
    throw std::invalid_argument(
        "Domain root must have exact multiplicative order equal to size");
  }
}

algebra::GRElem Domain::element(std::uint64_t i) const {
  if (i >= size_) {
    throw std::out_of_range("Domain::element index out of range");
  }

  return ctx_->with_ntl_context([&] {
    return offset_ * power(root_, CheckedLong(i, "domain index"));
  });
}

std::vector<algebra::GRElem> Domain::elements() const {
  std::vector<algebra::GRElem> out;
  out.reserve(static_cast<std::size_t>(size_));
  for (std::uint64_t i = 0; i < size_; ++i) {
    out.push_back(element(i));
  }
  return out;
}

Domain Domain::scale(std::uint64_t power_factor) const {
  if (power_factor == 0 || size_ % power_factor != 0) {
    throw std::invalid_argument("Domain::scale requires power dividing size");
  }

  return ctx_->with_ntl_context([&] {
    return Domain(ctx_, offset_, power(root_, CheckedLong(power_factor, "power")),
                  size_ / power_factor);
  });
}

Domain Domain::scale_offset(std::uint64_t power_factor) const {
  if (power_factor == 0 || size_ % power_factor != 0) {
    throw std::invalid_argument(
        "Domain::scale_offset requires power dividing size");
  }

  return ctx_->with_ntl_context([&] {
    return Domain(ctx_, offset_ * root_,
                  power(root_, CheckedLong(power_factor, "power")),
                  size_ / power_factor);
  });
}

Domain Domain::pow_map(std::uint64_t e) const {
  if (e == 0) {
    throw std::invalid_argument("Domain::pow_map requires e > 0");
  }

  const std::uint64_t common = std::gcd(size_, e);
  const std::uint64_t mapped_size = size_ / common;

  return ctx_->with_ntl_context([&] {
    return Domain(ctx_, power(offset_, CheckedLong(e, "pow_map exponent")),
                  power(root_, CheckedLong(e, "pow_map exponent")), mapped_size);
  });
}

bool Domain::disjoint_with(const Domain& other) const {
  const auto& lhs_cfg = context().config();
  const auto& rhs_cfg = other.context().config();
  if (lhs_cfg.p != rhs_cfg.p || lhs_cfg.k_exp != rhs_cfg.k_exp ||
      lhs_cfg.r != rhs_cfg.r) {
    throw std::invalid_argument("Domain::disjoint_with requires same ring");
  }

  const auto lhs = elements();
  const auto rhs = other.elements();

  for (const auto& x : lhs) {
    for (const auto& y : rhs) {
      if (x == y) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace swgr
