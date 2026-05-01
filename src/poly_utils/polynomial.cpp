#include "poly_utils/polynomial.hpp"

#include <NTL/ZZ_pE.h>

#include <utility>

using NTL::clear;

namespace stir_whir_gr::poly_utils {

Polynomial::Polynomial(std::vector<algebra::GRElem> coefficients)
    : coefficients_(std::move(coefficients)) {
  while (!coefficients_.empty() && coefficients_.back() == 0) {
    coefficients_.pop_back();
  }
}

std::size_t Polynomial::degree() const {
  if (coefficients_.empty()) {
    return 0;
  }

  for (std::size_t i = coefficients_.size(); i > 0; --i) {
    if (coefficients_[i - 1] != 0) {
      return i - 1;
    }
  }
  return 0;
}

algebra::GRElem Polynomial::evaluate(const algebra::GRContext& ctx,
                                     const algebra::GRElem& x) const {
  return ctx.with_ntl_context([&] {
    algebra::GRElem acc;
    clear(acc);

    for (auto it = coefficients_.rbegin(); it != coefficients_.rend(); ++it) {
      acc *= x;
      acc += *it;
    }
    return acc;
  });
}

}  // namespace stir_whir_gr::poly_utils
