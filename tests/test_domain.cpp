#include <exception>
#include <iostream>

#include "NTL/ZZ.h"
#include "NTL/ZZ_pE.h"

#include "algebra/teichmuller.hpp"
#include "domain.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

void TestDomainBasicShape() {
  testutil::PrintInfo("domain supports element access and derived subdomains");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 6});
  CHECK(swgr::algebra::teichmuller_subgroup_size_supported(ctx, 9));
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, 9);
  const auto coset_offset = ctx.teich_generator();
  const swgr::Domain coset =
      swgr::Domain::teichmuller_coset(ctx, coset_offset, 9);

  CHECK_EQ(domain.size(), std::uint64_t{9});
  CHECK_EQ(domain.element(0), ctx.one());
  CHECK_EQ(domain.scale(3).size(), std::uint64_t{3});
  CHECK_EQ(domain.scale_offset(3).size(), std::uint64_t{3});
  CHECK_EQ(domain.pow_map(3).size(), std::uint64_t{3});
  CHECK(domain.pow_map(3).disjoint_with(domain.scale_offset(3)));
  CHECK_EQ(coset.size(), domain.size());
  CHECK_EQ(coset.root(), domain.root());
  CHECK_EQ(coset.offset(), coset_offset);
}

void TestDomainRootOrderAndPowMap() {
  testutil::PrintInfo("domain root has exact order and pow_map semantics hold");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, 9);

  ctx.with_ntl_context([&] {
    CHECK_EQ(NTL::power(domain.root(), 9L), ctx.one());
    CHECK(domain.root() != ctx.one());
    CHECK_EQ(NTL::power(domain.root(), 3L), domain.scale(3).root());
    return 0;
  });

  CHECK(swgr::algebra::has_exact_multiplicative_order(ctx, domain.root(), 9));

  const swgr::Domain pow3 = domain.pow_map(3);
  const swgr::Domain pow9 = domain.pow_map(9);
  CHECK_EQ(pow3.size(), std::uint64_t{3});
  CHECK_EQ(pow9.size(), std::uint64_t{1});
  CHECK_EQ(pow9.element(0), ctx.one());
}

void TestTeichmullerMembershipHelpers() {
  testutil::PrintInfo(
      "teichmuller helper recognizes supported teichmuller elements and rejects nilpotent drift");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const auto domain = swgr::Domain::teichmuller_subgroup(ctx, 9);
  const auto coset =
      swgr::Domain::teichmuller_coset(ctx, ctx.teich_generator(), 9);

  auto two = ctx.with_ntl_context([&] { return ctx.one() + ctx.one(); });

  CHECK(swgr::algebra::is_teichmuller_element(ctx, ctx.zero()));
  CHECK(swgr::algebra::is_teichmuller_element(ctx, ctx.one()));
  CHECK(swgr::algebra::is_teichmuller_element(ctx, ctx.teich_generator()));
  CHECK(swgr::algebra::is_teichmuller_element(ctx, domain.element(4)));
  CHECK(swgr::algebra::is_teichmuller_element(ctx, coset.element(2)));
  CHECK(!swgr::algebra::is_teichmuller_element(ctx, two));
}

void TestDomainContainsHelper() {
  testutil::PrintInfo(
      "domain contains helper accepts in-domain teichmuller points and rejects outside points");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const swgr::Domain subgroup = swgr::Domain::teichmuller_subgroup(ctx, 9);
  const swgr::Domain coset =
      swgr::Domain::teichmuller_coset(ctx, ctx.teich_generator(), 9);

  auto two = ctx.with_ntl_context([&] { return ctx.one() + ctx.one(); });

  CHECK(subgroup.contains(ctx.one()));
  CHECK(subgroup.contains(subgroup.element(7)));
  CHECK(!subgroup.contains(coset.element(0)));
  CHECK(!subgroup.contains(ctx.zero()));
  CHECK(!subgroup.contains(two));

  CHECK(coset.contains(coset.element(3)));
  CHECK(!coset.contains(subgroup.element(0)));
  CHECK(!coset.contains(ctx.zero()));
}

void TestDomainTeichmullerSubsetHelper() {
  testutil::PrintInfo(
      "domain teichmuller-subset helper distinguishes teich cosets from generic unit cosets");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const swgr::Domain subgroup = swgr::Domain::teichmuller_subgroup(ctx, 9);
  const swgr::Domain teich_coset =
      swgr::Domain::teichmuller_coset(ctx, ctx.teich_generator(), 9);
  const auto non_teich_unit = ctx.with_ntl_context([&] {
    auto three = ctx.one();
    three += ctx.one();
    three += ctx.one();
    return three;
  });
  const swgr::Domain non_teich_coset =
      swgr::Domain::teichmuller_coset(ctx, non_teich_unit, 9);

  CHECK(subgroup.is_teichmuller_subset());
  CHECK(teich_coset.is_teichmuller_subset());
  CHECK(!non_teich_coset.is_teichmuller_subset());
  CHECK(non_teich_coset.contains(non_teich_coset.element(4)));
}

void TestDomainRejectsInvalidTeichmullerInputs() {
  testutil::PrintInfo(
      "teichmuller-only domain construction fails fast on invalid size or coset offset");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 6});

  bool bad_size_threw = false;
  try {
    (void)swgr::Domain::teichmuller_subgroup(ctx, 10);
  } catch (const std::invalid_argument&) {
    bad_size_threw = true;
  }
  CHECK(bad_size_threw);

  bool bad_offset_threw = false;
  try {
    (void)swgr::Domain::teichmuller_coset(ctx, ctx.zero(), 9);
  } catch (const std::invalid_argument&) {
    bad_offset_threw = true;
  }
  CHECK(bad_offset_threw);
}

void TestLargeExtensionTeichGeneratorFallback() {
  testutil::PrintInfo(
      "large-r teich generator remains usable for intended 3-smooth domains");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 162});
  CHECK(swgr::algebra::teichmuller_subgroup_size_supported(ctx, 81));

  const auto offset = ctx.teich_generator();
  CHECK(ctx.is_unit(offset));

  const swgr::Domain subgroup = swgr::Domain::teichmuller_subgroup(ctx, 81);
  const swgr::Domain coset = swgr::Domain::teichmuller_coset(ctx, offset, 81);

  CHECK(swgr::algebra::has_exact_multiplicative_order(ctx, subgroup.root(), 81));
  ctx.with_ntl_context([&] {
    CHECK_EQ(NTL::power(subgroup.root(), 81L), ctx.one());
    return 0;
  });

  CHECK_EQ(subgroup.size(), std::uint64_t{81});
  CHECK_EQ(coset.size(), std::uint64_t{81});
  CHECK_EQ(coset.root(), subgroup.root());
  CHECK_EQ(coset.offset(), offset);
}

void TestMidExtensionPrimitiveTeichGeneratorPath() {
  testutil::PrintInfo(
      "mid-r teich generator still supports deeper 3-smooth subgroup construction");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 18});
  CHECK(swgr::algebra::teichmuller_subgroup_size_supported(ctx, 27));

  const auto offset = ctx.teich_generator();
  CHECK(ctx.is_unit(offset));

  const swgr::Domain subgroup = swgr::Domain::teichmuller_subgroup(ctx, 27);
  const swgr::Domain coset = swgr::Domain::teichmuller_coset(ctx, offset, 27);

  CHECK_EQ(subgroup.size(), std::uint64_t{27});
  CHECK_EQ(coset.size(), std::uint64_t{27});
  CHECK_EQ(coset.offset(), offset);
}

void TestMidExtensionTeichGeneratorFallback() {
  testutil::PrintInfo(
      "mid-r teich fallback remains usable for preset-0 style subgroup construction");

  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 54});
  CHECK(swgr::algebra::teichmuller_subgroup_size_supported(ctx, 81));

  const auto offset = ctx.teich_generator();
  CHECK(ctx.is_unit(offset));

  const swgr::Domain subgroup = swgr::Domain::teichmuller_subgroup(ctx, 81);
  const swgr::Domain coset = swgr::Domain::teichmuller_coset(ctx, offset, 81);

  CHECK(swgr::algebra::has_exact_multiplicative_order(ctx, subgroup.root(), 81));
  ctx.with_ntl_context([&] {
    CHECK_EQ(NTL::power(subgroup.root(), 81L), ctx.one());
    return 0;
  });

  CHECK_EQ(subgroup.size(), std::uint64_t{81});
  CHECK_EQ(coset.size(), std::uint64_t{81});
  CHECK_EQ(coset.root(), subgroup.root());
  CHECK_EQ(coset.offset(), offset);
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestDomainBasicShape);
    RUN_TEST(TestDomainRootOrderAndPowMap);
    RUN_TEST(TestTeichmullerMembershipHelpers);
    RUN_TEST(TestDomainContainsHelper);
    RUN_TEST(TestDomainTeichmullerSubsetHelper);
    RUN_TEST(TestDomainRejectsInvalidTeichmullerInputs);
    RUN_TEST(TestLargeExtensionTeichGeneratorFallback);
    RUN_TEST(TestMidExtensionPrimitiveTeichGeneratorPath);
    RUN_TEST(TestMidExtensionTeichGeneratorFallback);
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled std::exception: " << ex.what() << "\n";
    return 2;
  }

  if (g_failures == 0) {
    std::cout << "\nAll tests passed.\n";
    return 0;
  }

  std::cerr << "\n" << g_failures << " test(s) failed.\n";
  return 1;
}
