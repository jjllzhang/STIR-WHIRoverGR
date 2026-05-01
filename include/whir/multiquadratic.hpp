#ifndef SWGR_WHIR_MULTIQUADRATIC_HPP_
#define SWGR_WHIR_MULTIQUADRATIC_HPP_

#include <cstdint>
#include <span>
#include <vector>

#include "algebra/gr_context.hpp"
#include "poly_utils/polynomial.hpp"

namespace swgr::whir {

std::uint64_t pow3_checked(std::uint64_t exponent);

std::uint64_t pow2_checked(std::uint64_t exponent);

std::uint64_t encode_base3_index(std::span<const std::uint8_t> digits);

std::vector<std::uint8_t> decode_base3_index(std::uint64_t index,
                                             std::uint64_t digit_count);

std::vector<algebra::GRElem> pow_m(const algebra::GRContext& ctx,
                                   const algebra::GRElem& x,
                                   std::uint64_t variable_count);

class MultiQuadraticPolynomial {
 public:
  MultiQuadraticPolynomial(std::uint64_t variable_count,
                           std::vector<algebra::GRElem> coefficients);

  std::uint64_t variable_count() const { return variable_count_; }
  const std::vector<algebra::GRElem>& coefficients() const {
    return coefficients_;
  }

  algebra::GRElem evaluate(const algebra::GRContext& ctx,
                           std::span<const algebra::GRElem> point) const;
  algebra::GRElem evaluate_pow(const algebra::GRContext& ctx,
                               const algebra::GRElem& x) const;
  MultiQuadraticPolynomial restrict_prefix(
      const algebra::GRContext& ctx,
      std::span<const algebra::GRElem> alphas) const;
  poly_utils::Polynomial to_univariate_pow_polynomial(
      const algebra::GRContext& ctx) const;

 private:
  std::uint64_t variable_count_ = 0;
  std::vector<algebra::GRElem> coefficients_;
};

class MultilinearPolynomial {
 public:
  MultilinearPolynomial(std::uint64_t variable_count,
                        std::vector<algebra::GRElem> coefficients);

  std::uint64_t variable_count() const { return variable_count_; }
  const std::vector<algebra::GRElem>& coefficients() const {
    return coefficients_;
  }

  algebra::GRElem evaluate(const algebra::GRContext& ctx,
                           std::span<const algebra::GRElem> point) const;
  MultiQuadraticPolynomial to_multi_quadratic(
      const algebra::GRContext& ctx) const;
  algebra::GRElem evaluate_pow(const algebra::GRContext& ctx,
                               const algebra::GRElem& x) const;

 private:
  std::uint64_t variable_count_ = 0;
  std::vector<algebra::GRElem> coefficients_;
};

}  // namespace swgr::whir

#endif  // SWGR_WHIR_MULTIQUADRATIC_HPP_
