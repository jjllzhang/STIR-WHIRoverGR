#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "NTL/ZZ.h"
#include "NTL/ZZ_pE.h"

#include "algebra/gr_context.hpp"
#include "algebra/gr_serialization.hpp"
#include "algebra/teichmuller.hpp"
#include "domain.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/polynomial.hpp"
#include "tests/test_common.hpp"

using swgr::algebra::GRConfig;
using swgr::algebra::GRContext;
using swgr::algebra::GRElem;
using swgr::algebra::deserialize_ring_element;
using swgr::algebra::serialize_ring_element;
using swgr::poly_utils::Polynomial;

int g_failures = 0;

namespace {

void TestInitGR216R54() {
  testutil::PrintInfo("GR(2^16,54) metadata initializes with expected byte width");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 54});
  CHECK_EQ(ctx.coeff_bytes(), std::size_t{2});
  CHECK_EQ(ctx.elem_bytes(), std::size_t{108});
}

void TestInitGR216R162() {
  testutil::PrintInfo("GR(2^16,162) context metadata initializes quickly");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});

  CHECK_EQ(ctx.coeff_bytes(), std::size_t{2});
  CHECK_EQ(ctx.elem_bytes(), std::size_t{324});
}

void TestInverseAndSerialization() {
  testutil::PrintInfo("inverse and serialization roundtrip on GR(2^16,2)");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const GRElem g = ctx.with_ntl_context([&] {
    NTL::ZZ_pX poly;
    NTL::SetCoeff(poly, 0, 1);
    NTL::SetCoeff(poly, 1, 1);
    GRElem unit;
    NTL::conv(unit, poly);
    return unit;
  });
  const GRElem g_inv = ctx.inv(g);

  ctx.with_ntl_context([&] {
    CHECK_EQ(g * g_inv, ctx.one());
    return 0;
  });

  const GRElem sample = ctx.with_ntl_context([&] { return ctx.one() + g; });
  const std::vector<std::uint8_t> encoded = serialize_ring_element(ctx, sample);
  const GRElem decoded = deserialize_ring_element(ctx, encoded);

  CHECK_EQ(encoded.size(), ctx.elem_bytes());
  CHECK_EQ(decoded, sample);
}

void TestInverseRejectsNonUnit() {
  testutil::PrintInfo("inverse rejects non-units with fail-fast behavior");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});

  bool threw = false;
  try {
    (void)ctx.inv(ctx.zero());
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void TestPolynomialDegreeAndInterpolation() {
  testutil::PrintInfo("polynomial degree trims zeros and interpolation roundtrips");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto coeffs = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{ctx.one(), ctx.zero(), ctx.zero()};
  });
  const Polynomial poly(coeffs);
  CHECK_EQ(poly.degree(), std::size_t{0});

  CHECK(swgr::algebra::teichmuller_subgroup_size_supported(ctx, 3));
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, 3);

  const auto poly2_coeffs = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{ctx.one(), domain.root(), ctx.one()};
  });
  const Polynomial poly2(poly2_coeffs);
  const auto evals = swgr::poly_utils::rs_encode(domain, poly2);
  const Polynomial recovered = swgr::poly_utils::rs_interpolate(domain, evals);

  CHECK_EQ(recovered.coefficients().size(), poly2.coefficients().size());
  for (std::size_t i = 0; i < recovered.coefficients().size(); ++i) {
    CHECK_EQ(recovered.coefficients()[i], poly2.coefficients()[i]);
  }
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestInitGR216R54);
    RUN_TEST(TestInitGR216R162);
    RUN_TEST(TestInverseAndSerialization);
    RUN_TEST(TestInverseRejectsNonUnit);
    RUN_TEST(TestPolynomialDegreeAndInterpolation);
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
