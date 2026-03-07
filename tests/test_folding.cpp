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

Polynomial SampleLongPolynomial(const GRContext& ctx, const Domain& domain,
                                std::size_t term_count) {
  const auto coeffs = ctx.with_ntl_context([&] {
    std::vector<GRElem> values;
    values.reserve(term_count);

    GRElem root_power = ctx.one();
    GRElem offset_power = ctx.one();
    const GRElem twist = domain.element(5);
    GRElem twist_power = ctx.one();
    for (std::size_t i = 0; i < term_count; ++i) {
      GRElem value = root_power + offset_power;
      if ((i % 3U) == 1U) {
        value += twist_power;
      }
      values.push_back(value);
      root_power *= domain.root();
      offset_power *= domain.offset();
      twist_power *= twist;
    }
    values.back() = domain.root();
    return values;
  });
  return Polynomial(coeffs);
}

void CheckFoldAgainstInterpolation(const Domain& domain, const Polynomial& poly,
                                   std::uint64_t k_fold,
                                   const GRElem& alpha) {
  const GRContext& ctx = domain.context();
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

void CheckFoldAgainstInterpolation(const Domain& domain, std::uint64_t k_fold,
                                   const GRElem& alpha) {
  CheckFoldAgainstInterpolation(domain, SamplePolynomial(domain.context(), domain),
                                k_fold, alpha);
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

void TestFoldTableKLargeRegression() {
  testutil::PrintInfo(
      "fold_table_k matches poly_fold + rs_encode on GR(2^16,162), n=243, k=9");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 243);
  const GRElem alpha =
      ctx.with_ntl_context([&] { return ctx.one() + domain.element(1) + domain.element(11); });
  const Polynomial poly =
      SampleLongPolynomial(ctx, domain, static_cast<std::size_t>(domain.size()));

  CheckFoldAgainstInterpolation(domain, poly, 9, alpha);
}

void TestFoldTableKRepeatedCallsStayStable() {
  testutil::PrintInfo(
      "fold_table_k stays stable across repeated calls on a shared GRContext");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 243);
  const Polynomial poly =
      SampleLongPolynomial(ctx, domain, static_cast<std::size_t>(domain.size()));
  const auto evals = swgr::poly_utils::rs_encode(domain, poly);
  const GRElem alpha =
      ctx.with_ntl_context([&] { return ctx.one() + domain.element(1) + domain.element(11); });
  const Polynomial folded_poly =
      ctx.with_ntl_context([&] { return swgr::poly_utils::poly_fold(poly, 9, alpha); });
  const auto expected =
      swgr::poly_utils::rs_encode(domain.pow_map(9), folded_poly);

  std::vector<GRElem> first_result;
  for (int iteration = 0; iteration < 6; ++iteration) {
    const auto folded =
        swgr::poly_utils::fold_table_k(domain, evals, 9, alpha);
    CHECK_EQ(folded.size(), expected.size());
    for (std::size_t i = 0; i < folded.size(); ++i) {
      CHECK_EQ(folded[i], expected[i]);
    }
    if (iteration == 0) {
      first_result = folded;
      continue;
    }
    CHECK_EQ(folded.size(), first_result.size());
    for (std::size_t i = 0; i < folded.size(); ++i) {
      CHECK_EQ(folded[i], first_result[i]);
    }
  }
}

void TestFoldEvalAtFiberPoint() {
  testutil::PrintInfo("fold_eval_k hits exact fiber values when alpha is on the fiber");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 9);

  CheckFoldAtFiberPoint(domain, 3, 1, 2);
  CheckFoldAtFiberPoint(domain, 9, 0, 4);
}

void TestFoldEvalGenericFallback() {
  testutil::PrintInfo("fold_eval_k generic batched-inversion fallback matches interpolation");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  ctx.with_ntl_context([&] {
    std::vector<GRElem> fiber_points;
    std::vector<GRElem> fiber_values;

    GRElem one = ctx.one();
    GRElem x;
    {
      NTL::ZZ_pX poly;
      NTL::SetCoeff(poly, 1, 1);
      NTL::conv(x, poly);
    }
    const GRElem one_plus_x = one + x;

    fiber_points.push_back(one);
    fiber_points.push_back(x);
    fiber_points.push_back(one_plus_x);

    fiber_values.push_back(one);
    fiber_values.push_back(one + one_plus_x);
    fiber_values.push_back(x + one_plus_x);

    const GRElem alpha = one + one + x;
    const Polynomial interpolated = swgr::poly_utils::interpolate_for_gr_wrapper(
        ctx, fiber_points, fiber_values);
    const GRElem direct =
        swgr::poly_utils::fold_eval_k(fiber_points, fiber_values, alpha);
    CHECK_EQ(direct, interpolated.evaluate(ctx, alpha));
    return 0;
  });
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestFoldingForK3AndK9);
    RUN_TEST(TestFoldTableKLargeRegression);
    RUN_TEST(TestFoldTableKRepeatedCallsStayStable);
    RUN_TEST(TestFoldEvalAtFiberPoint);
    RUN_TEST(TestFoldEvalGenericFallback);
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
