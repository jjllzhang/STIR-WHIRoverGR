#include "whir/folding.hpp"

#include <NTL/ZZ_pE.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "poly_utils/folding.hpp"

using NTL::power;

namespace stir_whir_gr::whir {
namespace {

std::uint64_t CheckedPow3(std::uint64_t exponent, const char* label) {
  std::uint64_t result = 1;
  for (std::uint64_t i = 0; i < exponent; ++i) {
    if (result > std::numeric_limits<std::uint64_t>::max() / 3U) {
      throw std::invalid_argument(std::string(label) + " overflows uint64_t");
    }
    result *= 3U;
  }
  return result;
}

std::vector<std::uint64_t> BuildVirtualFoldQueryIndices(
    std::uint64_t domain_size, std::uint64_t b, std::uint64_t child_index) {
  if (b == 0) {
    return {child_index};
  }

  const std::uint64_t next_domain_size = domain_size / 3U;
  const auto next_indices =
      BuildVirtualFoldQueryIndices(next_domain_size, b - 1U, child_index);

  std::vector<std::uint64_t> out;
  out.reserve(next_indices.size() * 3U);
  for (const std::uint64_t next_index : next_indices) {
    for (std::uint64_t offset = 0; offset < 3U; ++offset) {
      out.push_back(next_index + offset * next_domain_size);
    }
  }
  return out;
}

}  // namespace

std::vector<algebra::GRElem> repeated_ternary_fold_table(
    const Domain& domain, const std::vector<algebra::GRElem>& evals,
    std::span<const algebra::GRElem> alphas) {
  if (evals.size() != domain.size()) {
    throw std::invalid_argument(
        "repeated_ternary_fold_table requires eval count == domain size");
  }

  Domain current_domain = domain;
  std::vector<algebra::GRElem> current_evals = evals;
  for (const auto& alpha : alphas) {
    current_evals =
        poly_utils::fold_table_k(current_domain, current_evals, 3U, alpha);
    current_domain = current_domain.pow_map(3U);
  }
  return current_evals;
}

std::vector<std::uint64_t> virtual_fold_query_indices(
    std::uint64_t domain_size, std::uint64_t b, std::uint64_t child_index) {
  if (domain_size == 0) {
    throw std::invalid_argument(
        "virtual_fold_query_indices requires domain_size > 0");
  }

  const std::uint64_t fold_width = CheckedPow3(b, "virtual fold width");
  if (domain_size % fold_width != 0) {
    throw std::invalid_argument(
        "virtual_fold_query_indices requires 3^b dividing domain_size");
  }

  const std::uint64_t child_count = domain_size / fold_width;
  if (child_index >= child_count) {
    throw std::out_of_range(
        "virtual_fold_query_indices child_index out of range");
  }

  return BuildVirtualFoldQueryIndices(domain_size, b, child_index);
}

algebra::GRElem evaluate_repeated_ternary_fold_from_values(
    const std::vector<algebra::GRElem>& points,
    const std::vector<algebra::GRElem>& values,
    std::span<const algebra::GRElem> alphas) {
  if (points.size() != values.size()) {
    throw std::invalid_argument(
        "evaluate_repeated_ternary_fold_from_values requires equal-sized inputs");
  }

  const std::uint64_t expected_size =
      CheckedPow3(static_cast<std::uint64_t>(alphas.size()),
                  "repeated ternary sparse input size");
  if (static_cast<std::uint64_t>(points.size()) != expected_size) {
    throw std::invalid_argument(
        "evaluate_repeated_ternary_fold_from_values requires 3^b values");
  }
  if (values.empty()) {
    throw std::invalid_argument(
        "evaluate_repeated_ternary_fold_from_values requires non-empty inputs");
  }

  std::vector<algebra::GRElem> current_points = points;
  std::vector<algebra::GRElem> current_values = values;

  for (std::size_t level = 0; level < alphas.size(); ++level) {
    if (current_points.size() % 3U != 0) {
      throw std::invalid_argument(
          "evaluate_repeated_ternary_fold_from_values saw non-ternary level");
    }

    const std::size_t next_size = current_points.size() / 3U;
    std::vector<algebra::GRElem> next_points;
    std::vector<algebra::GRElem> next_values;
    next_points.reserve(next_size);
    next_values.reserve(next_size);

    for (std::size_t group = 0; group < next_size; ++group) {
      const std::size_t base = group * 3U;
      std::vector<algebra::GRElem> fiber_points{
          current_points[base], current_points[base + 1U],
          current_points[base + 2U]};
      std::vector<algebra::GRElem> fiber_values{
          current_values[base], current_values[base + 1U],
          current_values[base + 2U]};

      if (fiber_points[0] == fiber_points[1] ||
          fiber_points[0] == fiber_points[2] ||
          fiber_points[1] == fiber_points[2]) {
        throw std::invalid_argument(
            "evaluate_repeated_ternary_fold_from_values requires distinct "
            "fiber points");
      }

      const algebra::GRElem mapped_point = power(fiber_points[0], 3);
      if (power(fiber_points[1], 3) != mapped_point ||
          power(fiber_points[2], 3) != mapped_point) {
        throw std::invalid_argument(
            "evaluate_repeated_ternary_fold_from_values requires cube fibers");
      }

      next_points.push_back(mapped_point);
      next_values.push_back(
          poly_utils::fold_eval_k(fiber_points, fiber_values, alphas[level]));
    }

    current_points = std::move(next_points);
    current_values = std::move(next_values);
  }

  return current_values.front();
}

algebra::GRElem evaluate_virtual_fold_query_from_leaf_payloads(
    const Domain& domain, std::uint64_t b, std::uint64_t child_index,
    std::span<const std::vector<std::uint8_t>> leaf_payloads,
    std::span<const algebra::GRElem> alphas) {
  if (alphas.size() != b) {
    throw std::invalid_argument(
        "evaluate_virtual_fold_query_from_leaf_payloads requires one alpha per "
        "level");
  }

  const auto parent_indices =
      virtual_fold_query_indices(domain.size(), b, child_index);
  if (leaf_payloads.size() != parent_indices.size()) {
    throw std::invalid_argument(
        "evaluate_virtual_fold_query_from_leaf_payloads requires one payload per "
        "parent index");
  }

  return domain.context().with_ntl_context([&] {
    std::vector<algebra::GRElem> points;
    std::vector<algebra::GRElem> values;
    points.reserve(parent_indices.size());
    values.reserve(parent_indices.size());

    for (std::size_t i = 0; i < parent_indices.size(); ++i) {
      points.push_back(domain.element(parent_indices[i]));
      values.push_back(domain.context().deserialize(leaf_payloads[i]));
    }

    return evaluate_repeated_ternary_fold_from_values(points, values, alphas);
  });
}

}  // namespace stir_whir_gr::whir
