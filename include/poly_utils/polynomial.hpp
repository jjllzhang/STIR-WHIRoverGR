#ifndef SWGR_POLY_UTILS_POLYNOMIAL_HPP_
#define SWGR_POLY_UTILS_POLYNOMIAL_HPP_

#include <cstddef>
#include <vector>

#include "algebra/gr_context.hpp"

namespace swgr::poly_utils {

class Polynomial {
 public:
  Polynomial() = default;
  explicit Polynomial(std::vector<algebra::GRElem> coefficients);

  const std::vector<algebra::GRElem>& coefficients() const {
    return coefficients_;
  }

  bool empty() const { return coefficients_.empty(); }
  std::size_t size() const { return coefficients_.size(); }
  std::size_t degree() const;

  algebra::GRElem evaluate(const algebra::GRContext& ctx,
                           const algebra::GRElem& x) const;

 private:
  std::vector<algebra::GRElem> coefficients_;
};

}  // namespace swgr::poly_utils

#endif  // SWGR_POLY_UTILS_POLYNOMIAL_HPP_
