#include <exception>
#include <iostream>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/polynomial.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

using swgr::Domain;
using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;
using swgr::poly_utils::Polynomial;

Polynomial SamplePolynomial(const GRContext& ctx, const Domain& domain) {
  const auto coeffs = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{ctx.one(),
                               domain.root(),
                               ctx.one() + domain.root(),
                               domain.root() * domain.root(),
                               ctx.one(),
                               domain.root()};
  });
  return Polynomial(coeffs);
}

void CheckFoldAgainstInterpolation(const Domain& domain, std::uint64_t k_fold,
                                   const GRElem& alpha) {
  const GRContext& ctx = domain.context();
  const Polynomial poly = SamplePolynomial(ctx, domain);
  const auto evals = swgr::poly_utils::rs_encode(domain, poly);
  const auto folded_table =
      swgr::poly_utils::fold_table_k(domain, evals, k_fold, alpha);
  const Polynomial folded_poly =
      ctx.with_ntl_context([&] { return swgr::poly_utils::poly_fold(poly, k_fold, alpha); });
  const auto expected_table =
      swgr::poly_utils::rs_encode(domain.pow_map(k_fold), folded_poly);

  CHECK_EQ(folded_table.size(), expected_table.size());
  for (std::size_t i = 0; i < folded_table.size(); ++i) {
    CHECK_EQ(folded_table[i], expected_table[i]);
  }

  const std::uint64_t folded_size = domain.size() / k_fold;
  ctx.with_ntl_context([&] {
    for (std::uint64_t base = 0; base < folded_size; ++base) {
      std::vector<GRElem> fiber_points;
      std::vector<GRElem> fiber_values;
      for (std::uint64_t offset = 0; offset < k_fold; ++offset) {
        const std::uint64_t index = base + offset * folded_size;
        fiber_points.push_back(domain.element(index));
        fiber_values.push_back(evals[static_cast<std::size_t>(index)]);
      }
      const Polynomial interpolated =
          swgr::poly_utils::interpolate_for_gr_wrapper(ctx, fiber_points, fiber_values);
      const GRElem direct =
          swgr::poly_utils::fold_eval_k(fiber_points, fiber_values, alpha);
      CHECK_EQ(direct, interpolated.evaluate(ctx, alpha));
    }
    return 0;
  });
}

void CheckFoldAtFiberPoint(const Domain& domain, std::uint64_t k_fold,
                           std::uint64_t base_index,
                           std::uint64_t fiber_offset) {
  const GRContext& ctx = domain.context();
  const Polynomial poly = SamplePolynomial(ctx, domain);
  const auto evals = swgr::poly_utils::rs_encode(domain, poly);
  const std::uint64_t folded_size = domain.size() / k_fold;

  ctx.with_ntl_context([&] {
    std::vector<GRElem> fiber_points;
    std::vector<GRElem> fiber_values;
    fiber_points.reserve(static_cast<std::size_t>(k_fold));
    fiber_values.reserve(static_cast<std::size_t>(k_fold));
    for (std::uint64_t offset = 0; offset < k_fold; ++offset) {
      const std::uint64_t index = base_index + offset * folded_size;
      fiber_points.push_back(domain.element(index));
      fiber_values.push_back(evals[static_cast<std::size_t>(index)]);
    }

    const GRElem alpha = fiber_points[static_cast<std::size_t>(fiber_offset)];
    const GRElem direct =
        swgr::poly_utils::fold_eval_k(fiber_points, fiber_values, alpha);
    CHECK_EQ(direct, fiber_values[static_cast<std::size_t>(fiber_offset)]);
    return 0;
  });
}

void TestFoldingForK3AndK9() {
  testutil::PrintInfo("folding matches explicit interpolation for k=3 and k=9");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 9);
  const GRElem alpha = ctx.with_ntl_context([&] { return ctx.one() + domain.root(); });

  CheckFoldAgainstInterpolation(domain, 3, alpha);
  CheckFoldAgainstInterpolation(domain, 9, alpha);
}

void TestFoldEvalAtFiberPoint() {
  testutil::PrintInfo("fold_eval_k hits exact fiber values when alpha is on the fiber");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 9);

  CheckFoldAtFiberPoint(domain, 3, 1, 2);
  CheckFoldAtFiberPoint(domain, 9, 0, 4);
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestFoldingForK3AndK9);
    RUN_TEST(TestFoldEvalAtFiberPoint);
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled std::exception: " << ex.what() << "\n";
    return 2;
  } catch (...) {
    std::cerr << "Unhandled non-std exception\n";
    return 2;
  }

  if (g_failures == 0) {
    std::cout << "\nAll tests passed.\n";
    return 0;
  }

  std::cerr << "\n" << g_failures << " test(s) failed.\n";
  return 1;
}
