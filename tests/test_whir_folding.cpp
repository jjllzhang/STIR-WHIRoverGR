#include <NTL/ZZ_pE.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/polynomial.hpp"
#include "tests/test_common.hpp"
#include "whir/folding.hpp"

int g_failures = 0;

namespace {

using swgr::Domain;
using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;
using swgr::poly_utils::Polynomial;

Polynomial SamplePolynomial(const GRContext& ctx, const Domain& domain,
                            std::size_t coefficient_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(coefficient_count);

    GRElem root_power = ctx.one();
    GRElem offset_power = ctx.one();
    const GRElem twist = domain.element(1);
    GRElem twist_power = ctx.one();
    for (std::size_t i = 0; i < coefficient_count; ++i) {
      GRElem value = root_power + offset_power;
      if ((i % 3U) == 2U) {
        value += twist_power;
      }
      coefficients.push_back(value);
      root_power *= domain.root();
      offset_power *= domain.offset();
      twist_power *= twist;
    }

    coefficients.back() += domain.root();
    return Polynomial(coefficients);
  });
}

std::vector<GRElem> SparsePoints(const Domain& domain,
                                 const std::vector<std::uint64_t>& indices) {
  std::vector<GRElem> points;
  points.reserve(indices.size());
  for (const std::uint64_t index : indices) {
    points.push_back(domain.element(index));
  }
  return points;
}

std::vector<GRElem> SparseValues(const std::vector<GRElem>& evals,
                                 const std::vector<std::uint64_t>& indices) {
  std::vector<GRElem> values;
  values.reserve(indices.size());
  for (const std::uint64_t index : indices) {
    values.push_back(evals[static_cast<std::size_t>(index)]);
  }
  return values;
}

std::vector<std::vector<std::uint8_t>> SparsePayloads(
    const GRContext& ctx, const std::vector<GRElem>& evals,
    const std::vector<std::uint64_t>& indices) {
  std::vector<std::vector<std::uint8_t>> payloads;
  payloads.reserve(indices.size());
  for (const std::uint64_t index : indices) {
    payloads.push_back(ctx.serialize(evals[static_cast<std::size_t>(index)]));
  }
  return payloads;
}

void CheckSparseMatchesFullTable(const Domain& domain,
                                 const std::vector<GRElem>& evals,
                                 const std::vector<GRElem>& alphas) {
  const auto full =
      swgr::whir::repeated_ternary_fold_table(domain, evals, alphas);
  for (std::uint64_t child = 0; child < full.size(); ++child) {
    const auto indices = swgr::whir::virtual_fold_query_indices(
        domain.size(), static_cast<std::uint64_t>(alphas.size()), child);
    const auto points = SparsePoints(domain, indices);
    const auto values = SparseValues(evals, indices);

    const GRElem sparse = domain.context().with_ntl_context([&] {
      return swgr::whir::evaluate_repeated_ternary_fold_from_values(
          points, values, alphas);
    });
    CHECK_EQ(sparse, full[static_cast<std::size_t>(child)]);

    const auto payloads = SparsePayloads(domain.context(), evals, indices);
    const GRElem from_payloads =
        swgr::whir::evaluate_virtual_fold_query_from_leaf_payloads(
            domain, static_cast<std::uint64_t>(alphas.size()), child, payloads,
            alphas);
    CHECK_EQ(from_payloads, full[static_cast<std::size_t>(child)]);
  }
}

void TestRepeatedTernarySparseB1MatchesFullTable() {
  testutil::PrintInfo(
      "WHIR repeated ternary sparse fold matches full table for b=1");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 9);
  const Polynomial poly = SamplePolynomial(ctx, domain, domain.size());
  const auto evals = swgr::poly_utils::rs_encode(domain, poly);
  const std::vector<GRElem> alphas = ctx.with_ntl_context(
      [&] { return std::vector<GRElem>{ctx.one() + domain.element(2)}; });

  CheckSparseMatchesFullTable(domain, evals, alphas);
}

void TestRepeatedTernarySparseB2MatchesFullTable() {
  testutil::PrintInfo(
      "WHIR repeated ternary sparse fold matches full table for b=2");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 9);
  const Polynomial poly = SamplePolynomial(ctx, domain, domain.size());
  const auto evals = swgr::poly_utils::rs_encode(domain, poly);
  const std::vector<GRElem> alphas = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{ctx.one() + domain.element(2),
                               ctx.one() + domain.element(4)};
  });

  CheckSparseMatchesFullTable(domain, evals, alphas);
}

void TestRepeatedTernarySparseB3MatchesFullTable() {
  testutil::PrintInfo(
      "WHIR repeated ternary sparse fold matches full table for b=3");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 27);
  const Polynomial poly = SamplePolynomial(ctx, domain, domain.size());
  const auto evals = swgr::poly_utils::rs_encode(domain, poly);
  const std::vector<GRElem> alphas = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{ctx.one() + domain.element(2),
                               ctx.one() + domain.element(4),
                               ctx.one() + domain.element(8)};
  });

  CheckSparseMatchesFullTable(domain, evals, alphas);
}

void TestVirtualParentIndexShape() {
  testutil::PrintInfo(
      "WHIR virtual parent indices match H_i^(3^b) fibers");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 27);
  const Domain folded = domain.pow_map(9);
  const auto indices = swgr::whir::virtual_fold_query_indices(
      domain.size(), /*b=*/2, /*child_index=*/1);

  const std::vector<std::uint64_t> expected{
      1, 10, 19, 4, 13, 22, 7, 16, 25};
  CHECK_EQ(indices.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK_EQ(indices[i], expected[i]);
  }

  ctx.with_ntl_context([&] {
    const GRElem folded_point = folded.element(1);
    for (const std::uint64_t index : indices) {
      CHECK_EQ(NTL::power(domain.element(index), 9), folded_point);
    }
    return 0;
  });
}

void TestInvalidInputsReject() {
  testutil::PrintInfo("WHIR ternary folding helpers reject invalid inputs");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 9);
  const std::vector<GRElem> alphas = ctx.with_ntl_context(
      [&] { return std::vector<GRElem>{ctx.one() + domain.element(1)}; });

  bool threw = false;
  try {
    (void)swgr::whir::repeated_ternary_fold_table(
        domain, std::vector<GRElem>(8, ctx.one()), alphas);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    (void)swgr::whir::virtual_fold_query_indices(10, /*b=*/2,
                                                 /*child_index=*/0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    (void)swgr::whir::virtual_fold_query_indices(9, /*b=*/2,
                                                 /*child_index=*/1);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    ctx.with_ntl_context([&] {
      const std::vector<GRElem> points{domain.element(0), domain.element(3)};
      const std::vector<GRElem> values{ctx.one(), domain.root()};
      (void)swgr::whir::evaluate_repeated_ternary_fold_from_values(
          points, values, alphas);
      return 0;
    });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    ctx.with_ntl_context([&] {
      const std::vector<GRElem> points{ctx.one(), ctx.one(), domain.root()};
      const std::vector<GRElem> values{ctx.one(), domain.root(), ctx.one()};
      (void)swgr::whir::evaluate_repeated_ternary_fold_from_values(
          points, values, alphas);
      return 0;
    });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  threw = false;
  try {
    const std::vector<std::vector<std::uint8_t>> empty_payloads;
    (void)swgr::whir::evaluate_virtual_fold_query_from_leaf_payloads(
        domain, /*b=*/1, /*child_index=*/0, empty_payloads, alphas);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestRepeatedTernarySparseB1MatchesFullTable);
    RUN_TEST(TestRepeatedTernarySparseB2MatchesFullTable);
    RUN_TEST(TestRepeatedTernarySparseB3MatchesFullTable);
    RUN_TEST(TestVirtualParentIndexShape);
    RUN_TEST(TestInvalidInputsReject);
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
