#include <exception>
#include <iostream>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "poly_utils/fft3.hpp"
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
    return std::vector<GRElem>{
        ctx.one(),
        domain.root(),
        ctx.one() + domain.root(),
        domain.root() * domain.root(),
        ctx.one()};
  });
  return Polynomial(coeffs);
}

void TestFft3MatchesRSEncode() {
  testutil::PrintInfo("fft3 semantic first version matches rs_encode on subgroup and coset");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain subgroup = Domain::teichmuller_subgroup(ctx, 9);
  const Domain coset = Domain::teichmuller_coset(ctx, ctx.teich_generator(), 9);
  const Polynomial poly = SamplePolynomial(ctx, subgroup);

  const auto subgroup_fft = swgr::poly_utils::fft3(subgroup, poly);
  const auto subgroup_expected = swgr::poly_utils::rs_encode(subgroup, poly);
  CHECK_EQ(subgroup_fft.size(), subgroup_expected.size());
  for (std::size_t i = 0; i < subgroup_fft.size(); ++i) {
    CHECK_EQ(subgroup_fft[i], subgroup_expected[i]);
  }

  const auto coset_fft = swgr::poly_utils::fft3(coset, poly);
  const auto coset_expected = swgr::poly_utils::rs_encode(coset, poly);
  CHECK_EQ(coset_fft.size(), coset_expected.size());
  for (std::size_t i = 0; i < coset_fft.size(); ++i) {
    CHECK_EQ(coset_fft[i], coset_expected[i]);
  }
}

void TestInverseFft3Roundtrip() {
  testutil::PrintInfo("inverse_fft3 recovers coefficients up to domain padding");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain domain = Domain::teichmuller_subgroup(ctx, 9);
  const Polynomial poly = SamplePolynomial(ctx, domain);

  const auto evals = swgr::poly_utils::fft3(domain, poly);
  const auto recovered = swgr::poly_utils::inverse_fft3(domain, evals);

  CHECK_EQ(recovered.size(), std::size_t{9});
  for (std::size_t i = 0; i < poly.coefficients().size(); ++i) {
    CHECK_EQ(recovered[i], poly.coefficients()[i]);
  }
  for (std::size_t i = poly.coefficients().size(); i < recovered.size(); ++i) {
    CHECK_EQ(recovered[i], ctx.zero());
  }
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestFft3MatchesRSEncode);
    RUN_TEST(TestInverseFft3Roundtrip);
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
