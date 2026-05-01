#ifndef STIR_WHIR_GR_WHIR_CONSTRAINT_HPP_
#define STIR_WHIR_GR_WHIR_CONSTRAINT_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "algebra/gr_context.hpp"
#include "whir/common.hpp"
#include "whir/multiquadratic.hpp"

namespace stir_whir_gr::whir {

using TernaryGrid = std::array<algebra::GRElem, 3>;

struct EqTerm {
  algebra::GRElem weight;
  std::vector<algebra::GRElem> point;
};

using PointEvaluator =
    std::function<algebra::GRElem(std::span<const algebra::GRElem>)>;

TernaryGrid ternary_grid(const algebra::GRContext &ctx,
                         const algebra::GRElem &omega);

bool points_have_pairwise_unit_differences(
    const algebra::GRContext &ctx, std::span<const algebra::GRElem> points);

algebra::GRElem lagrange_basis_on_ternary_grid(const algebra::GRContext &ctx,
                                               const TernaryGrid &grid,
                                               std::size_t basis_index,
                                               const algebra::GRElem &x);

algebra::GRElem eq_B(const algebra::GRContext &ctx, const TernaryGrid &grid,
                     const algebra::GRElem &z, const algebra::GRElem &x);

algebra::GRElem eq_B(const algebra::GRContext &ctx, const TernaryGrid &grid,
                     std::span<const algebra::GRElem> z,
                     std::span<const algebra::GRElem> x);

std::vector<algebra::GRElem> sumcheck_interpolation_points(
    const algebra::GRContext &ctx);

class WhirConstraint {
 public:
  explicit WhirConstraint(TernaryGrid grid);
  WhirConstraint(TernaryGrid grid, std::vector<EqTerm> terms);

  const TernaryGrid &grid() const { return grid_; }
  const std::vector<EqTerm> &terms() const { return terms_; }
  bool empty() const { return terms_.empty(); }
  std::uint64_t variable_count() const;

  algebra::GRElem evaluate_A(const algebra::GRContext &ctx,
                             std::span<const algebra::GRElem> x) const;
  algebra::GRElem evaluate_W(const algebra::GRContext &ctx,
                             const algebra::GRElem &z,
                             std::span<const algebra::GRElem> x) const;
  WhirConstraint restrict_prefix(const algebra::GRContext &ctx,
                                 std::span<const algebra::GRElem> alphas) const;
  void add_shift_term(algebra::GRElem weight,
                      std::vector<algebra::GRElem> point);

 private:
  TernaryGrid grid_;
  std::vector<EqTerm> terms_;
};

WhirSumcheckPolynomial honest_sumcheck_polynomial(
    const algebra::GRContext &ctx, std::uint64_t variable_count,
    const WhirConstraint &constraint, std::span<const algebra::GRElem> prefix,
    const PointEvaluator &evaluate_f);

WhirSumcheckPolynomial honest_sumcheck_polynomial(
    const algebra::GRContext &ctx, const MultiQuadraticPolynomial &polynomial,
    const WhirConstraint &constraint, std::span<const algebra::GRElem> prefix);

std::size_t sumcheck_declared_degree(const WhirSumcheckPolynomial &polynomial);

algebra::GRElem evaluate_sumcheck_polynomial(
    const algebra::GRContext &ctx, const WhirSumcheckPolynomial &polynomial,
    const algebra::GRElem &x);

bool check_sumcheck_degree(const WhirSumcheckPolynomial &polynomial,
                           std::uint64_t degree_bound);

algebra::GRElem sumcheck_grid_sum(const algebra::GRContext &ctx,
                                  const TernaryGrid &grid,
                                  const WhirSumcheckPolynomial &polynomial);

bool check_sumcheck_identity(const algebra::GRContext &ctx,
                             const TernaryGrid &grid,
                             const WhirSumcheckPolynomial &polynomial,
                             const algebra::GRElem &current_sigma,
                             std::uint64_t degree_bound);

algebra::GRElem sumcheck_next_sigma(const algebra::GRContext &ctx,
                                    const WhirSumcheckPolynomial &polynomial,
                                    const algebra::GRElem &alpha);

}  // namespace stir_whir_gr::whir

#endif  // STIR_WHIR_GR_WHIR_CONSTRAINT_HPP_
