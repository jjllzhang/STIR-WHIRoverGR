#include <atomic>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;
using stir_whir_gr::algebra::GRElem;
using stir_whir_gr::algebra::deserialize_ring_element;
using stir_whir_gr::algebra::serialize_ring_element;
using stir_whir_gr::poly_utils::Polynomial;

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

void TestBatchInverse() {
  testutil::PrintInfo("batch_inv computes inverses with one API call");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto units = ctx.with_ntl_context([&] {
    std::vector<GRElem> values;

    NTL::ZZ_pX poly0;
    NTL::SetCoeff(poly0, 0, 1);
    NTL::SetCoeff(poly0, 1, 1);
    GRElem value0;
    NTL::conv(value0, poly0);
    values.push_back(value0);

    NTL::ZZ_pX poly1;
    NTL::SetCoeff(poly1, 0, 3);
    NTL::SetCoeff(poly1, 1, 2);
    GRElem value1;
    NTL::conv(value1, poly1);
    values.push_back(value1);

    NTL::ZZ_pX poly2;
    NTL::SetCoeff(poly2, 0, 5);
    NTL::SetCoeff(poly2, 1, 7);
    GRElem value2;
    NTL::conv(value2, poly2);
    values.push_back(value2);

    return values;
  });

  const auto inverses = ctx.batch_inv(units);
  CHECK_EQ(inverses.size(), units.size());
  ctx.with_ntl_context([&] {
    for (std::size_t i = 0; i < units.size(); ++i) {
      CHECK_EQ(units[i] * inverses[i], ctx.one());
    }
    return 0;
  });
}

void TestBatchInverseEmpty() {
  testutil::PrintInfo("batch_inv preserves empty inputs");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const std::vector<GRElem> empty;
  const auto inverses = ctx.batch_inv(empty);
  CHECK(inverses.empty());
}

void TestBatchInverseRejectsNonUnit() {
  testutil::PrintInfo("batch_inv rejects non-units with a clear error");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto inputs = ctx.with_ntl_context([&] {
    std::vector<GRElem> values;
    values.push_back(ctx.one());
    values.push_back(ctx.zero());
    return values;
  });

  bool threw = false;
  try {
    (void)ctx.batch_inv(inputs);
  } catch (const std::invalid_argument& ex) {
    threw = true;
    CHECK(std::string(ex.what()).find("unit") != std::string::npos);
  }
  CHECK(threw);
}

void TestInterpolationRejectsNonExceptionalPointSet() {
  testutil::PrintInfo(
      "interpolation rejects ring point sets whose pairwise difference is a non-unit");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 2});
  const auto [points, values] = ctx.with_ntl_context([&] {
    std::vector<GRElem> test_points;
    std::vector<GRElem> test_values;

    const GRElem one = ctx.one();
    const GRElem two = one + one;
    const GRElem three = two + one;

    test_points.push_back(one);
    test_points.push_back(three);
    test_values.push_back(one);
    test_values.push_back(two);
    return std::pair(std::move(test_points), std::move(test_values));
  });

  bool threw = false;
  try {
    (void)stir_whir_gr::poly_utils::interpolate_for_gr_wrapper(ctx, points, values);
  } catch (const std::invalid_argument& ex) {
    threw = true;
    CHECK(std::string(ex.what()).find("exceptional point set") !=
          std::string::npos);
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

  CHECK(stir_whir_gr::algebra::teichmuller_subgroup_size_supported(ctx, 3));
  const stir_whir_gr::Domain domain = stir_whir_gr::Domain::teichmuller_subgroup(ctx, 3);

  const auto poly2_coeffs = ctx.with_ntl_context([&] {
    return std::vector<GRElem>{ctx.one(), domain.root(), ctx.one()};
  });
  const Polynomial poly2(poly2_coeffs);
  const auto evals = stir_whir_gr::poly_utils::rs_encode(domain, poly2);
  const Polynomial recovered = stir_whir_gr::poly_utils::rs_interpolate(domain, evals);

  CHECK_EQ(recovered.coefficients().size(), poly2.coefficients().size());
  for (std::size_t i = 0; i < recovered.coefficients().size(); ++i) {
    CHECK_EQ(recovered.coefficients()[i], poly2.coefficients()[i]);
  }
}

void TestThreadSafeLazyInitializationOnSharedContext() {
  testutil::PrintInfo(
      "shared GRContext lazy initialization is safe under concurrent first use");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  constexpr std::size_t kThreadCount = 8;
  constexpr std::size_t kIterationsPerThread = 8;
  std::atomic<bool> start(false);
  std::atomic<bool> failed(false);
  std::mutex error_mutex;
  std::string first_error;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  for (std::size_t thread_index = 0; thread_index < kThreadCount; ++thread_index) {
    threads.emplace_back([&, thread_index] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      try {
        for (std::size_t iteration = 0; iteration < kIterationsPerThread;
             ++iteration) {
          if ((thread_index + iteration) % 2U == 0U) {
            const auto generator = ctx.teich_generator();
            if (!ctx.is_unit(generator)) {
              throw std::runtime_error("teich generator should be a unit");
            }
          } else {
            const auto encoded = ctx.serialize(ctx.one());
            const auto decoded = ctx.deserialize(encoded);
            if (decoded != ctx.one()) {
              throw std::runtime_error(
                  "shared context serialize/deserialize roundtrip failed");
            }
          }

          (void)ctx.base_irreducible_mod_p();
          (void)ctx.extension_polynomial();
        }
      } catch (const std::exception& ex) {
        failed.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(error_mutex);
        if (first_error.empty()) {
          first_error = ex.what();
        }
      } catch (...) {
        failed.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(error_mutex);
        if (first_error.empty()) {
          first_error = "non-std exception";
        }
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }

  if (failed.load(std::memory_order_acquire)) {
    throw std::runtime_error(first_error);
  }
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestInitGR216R54);
    RUN_TEST(TestInitGR216R162);
    RUN_TEST(TestInverseAndSerialization);
    RUN_TEST(TestInverseRejectsNonUnit);
    RUN_TEST(TestBatchInverse);
    RUN_TEST(TestBatchInverseEmpty);
    RUN_TEST(TestBatchInverseRejectsNonUnit);
    RUN_TEST(TestInterpolationRejectsNonExceptionalPointSet);
    RUN_TEST(TestPolynomialDegreeAndInterpolation);
    RUN_TEST(TestThreadSafeLazyInitializationOnSharedContext);
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
