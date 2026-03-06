#include "poly_utils/folding.hpp"

#include <NTL/ZZ_pE.h>

#include <stdexcept>
#include <vector>

#include "GaloisRing/Inverse.hpp"

using NTL::ZZ_pE;
using NTL::clear;
using NTL::deg;

namespace swgr::poly_utils {
namespace {

long CurrentExtensionDegree() {
  return deg(ZZ_pE::modulus());
}

}  // namespace

Polynomial poly_fold(const Polynomial& f, std::uint64_t folding_factor_k,
                     const algebra::GRElem& alpha) {
  if (folding_factor_k == 0) {
    throw std::invalid_argument("poly_fold requires folding_factor_k > 0");
  }
  if (f.empty()) {
    return Polynomial{};
  }

  const auto& coefficients = f.coefficients();
  const std::size_t folded_size =
      (coefficients.size() + static_cast<std::size_t>(folding_factor_k) - 1U) /
      static_cast<std::size_t>(folding_factor_k);

  std::vector<algebra::GRElem> folded(folded_size);
  for (auto& coefficient : folded) {
    clear(coefficient);
  }

  algebra::GRElem alpha_power;
  NTL::set(alpha_power);
  for (std::uint64_t residue = 0; residue < folding_factor_k; ++residue) {
    for (std::size_t index = static_cast<std::size_t>(residue);
         index < coefficients.size();
         index += static_cast<std::size_t>(folding_factor_k)) {
      folded[index / static_cast<std::size_t>(folding_factor_k)] +=
          alpha_power * coefficients[index];
    }
    alpha_power *= alpha;
  }

  return Polynomial(std::move(folded));
}

algebra::GRElem fold_eval_k(
    const std::vector<algebra::GRElem>& fiber_points,
    const std::vector<algebra::GRElem>& fiber_values,
    const algebra::GRElem& alpha) {
  if (fiber_points.empty()) {
    throw std::invalid_argument("fold_eval_k requires non-empty fiber");
  }
  if (fiber_points.size() != fiber_values.size()) {
    throw std::invalid_argument("fold_eval_k requires equal-sized fiber inputs");
  }

  algebra::GRElem result;
  clear(result);
  const long extension_degree = CurrentExtensionDegree();

  for (std::size_t i = 0; i < fiber_points.size(); ++i) {
    algebra::GRElem basis;
    NTL::set(basis);

    for (std::size_t j = 0; j < fiber_points.size(); ++j) {
      if (i == j) {
        continue;
      }

      const algebra::GRElem denominator = fiber_points[i] - fiber_points[j];
      const algebra::GRElem denominator_inv = Inv(denominator, extension_degree);
      if (denominator_inv == 0) {
        throw std::invalid_argument("fold_eval_k requires exceptional fiber");
      }
      basis *= (alpha - fiber_points[j]) * denominator_inv;
    }

    result += fiber_values[i] * basis;
  }

  return result;
}

std::vector<algebra::GRElem> fold_table_k(
    const Domain& domain, const std::vector<algebra::GRElem>& evals,
    std::uint64_t k_fold, const algebra::GRElem& alpha) {
  if (k_fold == 0) {
    throw std::invalid_argument("fold_table_k requires k_fold > 0");
  }
  if (evals.size() != domain.size()) {
    throw std::invalid_argument("fold_table_k requires eval count == domain size");
  }
  if (domain.size() % k_fold != 0) {
    throw std::invalid_argument("fold_table_k requires k_fold dividing domain size");
  }

  const std::uint64_t folded_size = domain.size() / k_fold;
  std::vector<algebra::GRElem> out;
  out.reserve(static_cast<std::size_t>(folded_size));

  return domain.context().with_ntl_context([&] {
    std::vector<algebra::GRElem> fiber_points;
    std::vector<algebra::GRElem> fiber_values;
    fiber_points.reserve(static_cast<std::size_t>(k_fold));
    fiber_values.reserve(static_cast<std::size_t>(k_fold));

    for (std::uint64_t base = 0; base < folded_size; ++base) {
      fiber_points.clear();
      fiber_values.clear();
      for (std::uint64_t offset = 0; offset < k_fold; ++offset) {
        const std::uint64_t index = base + offset * folded_size;
        fiber_points.push_back(domain.element(index));
        fiber_values.push_back(evals[static_cast<std::size_t>(index)]);
      }
      out.push_back(fold_eval_k(fiber_points, fiber_values, alpha));
    }
    return out;
  });
}

}  // namespace swgr::poly_utils
