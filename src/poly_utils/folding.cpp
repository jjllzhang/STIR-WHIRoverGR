#include "poly_utils/folding.hpp"

#include <NTL/ZZ_pE.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "GaloisRing/Inverse.hpp"

using NTL::ZZ_pE;
using NTL::clear;
using NTL::deg;
using NTL::power;

namespace swgr::poly_utils {
namespace {

long CurrentExtensionDegree() {
  return deg(ZZ_pE::modulus());
}

algebra::GRElem ConstantFromUint64(std::uint64_t value) {
  algebra::GRElem out;
  clear(out);
  algebra::GRElem one;
  NTL::set(one);
  for (std::uint64_t i = 0; i < value; ++i) {
    out += one;
  }
  return out;
}

struct StructuredFiberCache {
  std::vector<algebra::GRElem> roots;
  std::vector<algebra::GRElem> denominator_inverses;
};

StructuredFiberCache BuildStructuredFiberCache(
    std::uint64_t fiber_size, const algebra::GRElem& fiber_root,
    long extension_degree) {
  StructuredFiberCache cache;
  cache.roots.reserve(static_cast<std::size_t>(fiber_size));
  cache.denominator_inverses.reserve(static_cast<std::size_t>(fiber_size));

  const algebra::GRElem size_as_elem = ConstantFromUint64(fiber_size);
  const algebra::GRElem size_inv = Inv(size_as_elem, extension_degree);
  if (size_inv == 0) {
    throw std::invalid_argument(
        "structured folding requires invertible fiber size");
  }

  algebra::GRElem current_root;
  NTL::set(current_root);
  for (std::uint64_t i = 0; i < fiber_size; ++i) {
    cache.roots.push_back(current_root);
    cache.denominator_inverses.push_back(current_root * size_inv);
    current_root *= fiber_root;
  }
  return cache;
}

algebra::GRElem EvaluateStructuredFiber(
    const StructuredFiberCache& cache,
    const std::vector<algebra::GRElem>& fiber_values,
    const algebra::GRElem& scaled_alpha,
    std::vector<algebra::GRElem>* differences,
    std::vector<algebra::GRElem>* prefix_products,
    std::vector<algebra::GRElem>* suffix_products) {
  if (cache.roots.size() != fiber_values.size()) {
    throw std::invalid_argument(
        "EvaluateStructuredFiber requires matching cache/value sizes");
  }

  const std::size_t fiber_size = cache.roots.size();
  if (fiber_size == 0) {
    throw std::invalid_argument("EvaluateStructuredFiber requires non-empty fiber");
  }

  differences->resize(fiber_size);
  prefix_products->resize(fiber_size + 1U);
  suffix_products->resize(fiber_size + 1U);

  for (std::size_t i = 0; i < fiber_size; ++i) {
    (*differences)[i] = scaled_alpha - cache.roots[i];
  }

  NTL::set((*prefix_products)[0]);
  for (std::size_t i = 0; i < fiber_size; ++i) {
    (*prefix_products)[i + 1U] = (*prefix_products)[i] * (*differences)[i];
  }

  NTL::set((*suffix_products)[fiber_size]);
  for (std::size_t i = fiber_size; i > 0; --i) {
    (*suffix_products)[i - 1U] = (*suffix_products)[i] * (*differences)[i - 1U];
  }

  algebra::GRElem result;
  clear(result);
  for (std::size_t i = 0; i < fiber_size; ++i) {
    const algebra::GRElem numerator =
        (*prefix_products)[i] * (*suffix_products)[i + 1U];
    result += fiber_values[i] * numerator * cache.denominator_inverses[i];
  }
  return result;
}

bool TryBuildStructuredFiberCache(
    const std::vector<algebra::GRElem>& fiber_points, long extension_degree,
    algebra::GRElem* base_inverse, StructuredFiberCache* cache) {
  if (fiber_points.empty()) {
    return false;
  }

  const algebra::GRElem candidate_base_inverse =
      Inv(fiber_points.front(), extension_degree);
  if (candidate_base_inverse == 0) {
    return false;
  }

  if (fiber_points.size() == 1U) {
    *base_inverse = candidate_base_inverse;
    *cache = BuildStructuredFiberCache(1, ConstantFromUint64(1), extension_degree);
    return true;
  }

  const algebra::GRElem candidate_root = fiber_points[1] * candidate_base_inverse;
  algebra::GRElem current_power;
  NTL::set(current_power);
  for (std::size_t i = 0; i < fiber_points.size(); ++i) {
    if (fiber_points[i] != fiber_points.front() * current_power) {
      return false;
    }
    current_power *= candidate_root;
  }
  if (current_power != ConstantFromUint64(1)) {
    return false;
  }

  *base_inverse = candidate_base_inverse;
  *cache = BuildStructuredFiberCache(
      static_cast<std::uint64_t>(fiber_points.size()), candidate_root,
      extension_degree);
  return true;
}

algebra::GRElem FoldEvalNaive(
    const std::vector<algebra::GRElem>& fiber_points,
    const std::vector<algebra::GRElem>& fiber_values,
    const algebra::GRElem& alpha, long extension_degree) {
  algebra::GRElem result;
  clear(result);

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

  const long extension_degree = CurrentExtensionDegree();
  algebra::GRElem base_inverse;
  StructuredFiberCache cache;
  if (TryBuildStructuredFiberCache(fiber_points, extension_degree, &base_inverse,
                                   &cache)) {
    std::vector<algebra::GRElem> differences;
    std::vector<algebra::GRElem> prefix_products;
    std::vector<algebra::GRElem> suffix_products;
    return EvaluateStructuredFiber(cache, fiber_values, alpha * base_inverse,
                                   &differences, &prefix_products,
                                   &suffix_products);
  }

  return FoldEvalNaive(fiber_points, fiber_values, alpha, extension_degree);
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
    const long extension_degree = CurrentExtensionDegree();
    const algebra::GRElem fiber_root = power(
        domain.root(), static_cast<long>(folded_size));
    const StructuredFiberCache cache =
        BuildStructuredFiberCache(k_fold, fiber_root, extension_degree);

    std::vector<algebra::GRElem> fiber_values(static_cast<std::size_t>(k_fold));
    std::vector<algebra::GRElem> differences;
    std::vector<algebra::GRElem> prefix_products;
    std::vector<algebra::GRElem> suffix_products;

    const algebra::GRElem root_inverse = domain.context().inv(domain.root());
    algebra::GRElem base_inverse = domain.context().inv(domain.offset());
    for (std::uint64_t base = 0; base < folded_size; ++base) {
      for (std::uint64_t offset = 0; offset < k_fold; ++offset) {
        const std::uint64_t index = base + offset * folded_size;
        fiber_values[static_cast<std::size_t>(offset)] =
            evals[static_cast<std::size_t>(index)];
      }
      out.push_back(EvaluateStructuredFiber(
          cache, fiber_values, alpha * base_inverse, &differences,
          &prefix_products, &suffix_products));
      base_inverse *= root_inverse;
    }
    return out;
  });
}

}  // namespace swgr::poly_utils
