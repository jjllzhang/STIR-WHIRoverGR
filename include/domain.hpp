#ifndef SWGR_DOMAIN_HPP_
#define SWGR_DOMAIN_HPP_

#include <cstdint>
#include <memory>
#include <vector>

#include "algebra/teichmuller.hpp"
#include "algebra/gr_context.hpp"

namespace swgr {

class Domain {
 public:
  static Domain teichmuller_subgroup(const algebra::GRContext& ctx,
                                     std::uint64_t size);
  static Domain teichmuller_subgroup(
      std::shared_ptr<const algebra::GRContext> ctx, std::uint64_t size);
  static Domain teichmuller_coset(const algebra::GRContext& ctx,
                                  algebra::GRElem offset,
                                  std::uint64_t size);
  static Domain teichmuller_coset(
      std::shared_ptr<const algebra::GRContext> ctx, algebra::GRElem offset,
      std::uint64_t size);

  const algebra::GRContext& context() const { return *ctx_; }
  const std::shared_ptr<const algebra::GRContext>& context_ptr() const {
    return ctx_;
  }
  std::uint64_t size() const { return size_; }
  const algebra::GRElem& offset() const { return offset_; }
  const algebra::GRElem& root() const { return root_; }

  algebra::GRElem element(std::uint64_t i) const;
  std::vector<algebra::GRElem> elements() const;
  bool contains(const algebra::GRElem& value) const;
  bool is_teichmuller_subset() const;

  Domain scale(std::uint64_t power) const;
  Domain scale_offset(std::uint64_t power) const;
  Domain pow_map(std::uint64_t e) const;
  bool disjoint_with(const Domain& other) const;

 private:
  Domain(const algebra::GRContext& ctx, algebra::GRElem offset,
         algebra::GRElem root, std::uint64_t size);
  Domain(std::shared_ptr<const algebra::GRContext> ctx, algebra::GRElem offset,
         algebra::GRElem root, std::uint64_t size);

  std::shared_ptr<const algebra::GRContext> ctx_;
  algebra::GRElem offset_;
  algebra::GRElem root_;
  std::uint64_t size_;
};

}  // namespace swgr

#endif  // SWGR_DOMAIN_HPP_
