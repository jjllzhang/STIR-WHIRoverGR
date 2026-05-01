#include <atomic>
#include <exception>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "NTL/ZZ.h"
#include "NTL/ZZ_pE.h"

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "poly_utils/fft3.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/polynomial.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

using stir_whir_gr::Domain;
using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;
using stir_whir_gr::algebra::GRElem;
using stir_whir_gr::poly_utils::Polynomial;

std::vector<GRElem> NaiveRSEncode(const Domain& domain, const Polynomial& poly) {
  const auto points = domain.elements();
  std::vector<GRElem> evals;
  evals.reserve(points.size());
  for (const auto& point : points) {
    evals.push_back(poly.evaluate(domain.context(), point));
  }
  return evals;
}

GRElem SampleRandomRingElement(const GRContext& ctx, std::mt19937_64& rng) {
  return ctx.with_ntl_context([&] {
    NTL::SetSeed(NTL::to_ZZ(static_cast<long>(rng())));
    GRElem value;
    random(value);
    return value;
  });
}

Polynomial SampleRandomPolynomial(const GRContext& ctx, std::size_t term_count,
                                  std::mt19937_64& rng) {
  std::vector<GRElem> coefficients;
  coefficients.reserve(term_count);
  for (std::size_t i = 0; i < term_count; ++i) {
    coefficients.push_back(SampleRandomRingElement(ctx, rng));
  }
  return Polynomial(std::move(coefficients));
}

std::vector<GRElem> SampleRandomEvaluations(const GRContext& ctx,
                                            std::size_t count,
                                            std::mt19937_64& rng) {
  std::vector<GRElem> evals;
  evals.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    evals.push_back(SampleRandomRingElement(ctx, rng));
  }
  return evals;
}

Polynomial SamplePolynomial(const GRContext& ctx, const Domain& domain,
                            std::size_t term_count = 5) {
  const auto coeffs = ctx.with_ntl_context([&] {
    std::vector<GRElem> values;
    values.reserve(term_count);

    GRElem root_power = ctx.one();
    GRElem offset_power = ctx.one();
    for (std::size_t i = 0; i < term_count; ++i) {
      values.push_back(root_power + offset_power);
      root_power *= domain.root();
      offset_power *= domain.offset();
    }
    return values;
  });
  return Polynomial(coeffs);
}

void AppendArithmeticProgressionIndices(std::vector<std::size_t>* indices,
                                        std::size_t start,
                                        std::size_t stride,
                                        std::size_t limit) {
  for (std::size_t index = start; index < limit; index += stride) {
    indices->push_back(index);
  }
}

Polynomial MakeSparsePolynomial(const GRContext& ctx, std::size_t term_count,
                                const std::vector<std::size_t>& nonzero_indices,
                                std::mt19937_64& rng) {
  std::vector<GRElem> coefficients(term_count, ctx.zero());
  for (const std::size_t index : nonzero_indices) {
    if (index >= term_count) {
      throw std::invalid_argument("sparse coefficient index exceeds term count");
    }
    coefficients[index] = SampleRandomRingElement(ctx, rng);
  }
  return Polynomial(std::move(coefficients));
}

void CheckFftMatchesRSEncode(const Domain& domain, const Polynomial& poly) {
  const auto fft_evals = stir_whir_gr::poly_utils::fft3(domain, poly);
  const auto expected = NaiveRSEncode(domain, poly);

  CHECK_EQ(fft_evals.size(), expected.size());
  for (std::size_t i = 0; i < fft_evals.size(); ++i) {
    CHECK_EQ(fft_evals[i], expected[i]);
  }
}

void CheckInverseRoundtrip(const Domain& domain, const Polynomial& poly) {
  const auto evals = stir_whir_gr::poly_utils::fft3(domain, poly);
  const auto recovered = stir_whir_gr::poly_utils::inverse_fft3(domain, evals);

  CHECK_EQ(recovered.size(), static_cast<std::size_t>(domain.size()));
  for (std::size_t i = 0; i < poly.coefficients().size(); ++i) {
    CHECK_EQ(recovered[i], poly.coefficients()[i]);
  }
  for (std::size_t i = poly.coefficients().size(); i < recovered.size(); ++i) {
    CHECK_EQ(recovered[i], domain.context().zero());
  }
}

void CheckInverseMatchesInterpolation(const Domain& domain,
                                      const Polynomial& poly) {
  const auto evals = stir_whir_gr::poly_utils::fft3(domain, poly);
  const auto recovered = stir_whir_gr::poly_utils::inverse_fft3(domain, evals);
  const auto expected_poly = stir_whir_gr::poly_utils::interpolate_for_gr_wrapper(
      domain.context(), domain.elements(), evals);
  std::vector<GRElem> expected = expected_poly.coefficients();
  expected.resize(static_cast<std::size_t>(domain.size()),
                  domain.context().zero());

  CHECK_EQ(recovered.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK_EQ(recovered[i], expected[i]);
  }
}

void CheckInverseMatchesInterpolation(
    const Domain& domain, const std::vector<GRElem>& evals) {
  const auto recovered = stir_whir_gr::poly_utils::inverse_fft3(domain, evals);
  const auto expected_poly = stir_whir_gr::poly_utils::interpolate_for_gr_wrapper(
      domain.context(), domain.elements(), evals);
  std::vector<GRElem> expected = expected_poly.coefficients();
  expected.resize(static_cast<std::size_t>(domain.size()),
                  domain.context().zero());

  CHECK_EQ(recovered.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK_EQ(recovered[i], expected[i]);
  }
}

void CheckRepeatedCallsStable(const Domain& domain, const Polynomial& poly,
                              const std::vector<GRElem>& evals,
                              std::size_t repeat_count) {
  const auto expected_evals = NaiveRSEncode(domain, poly);
  const auto expected_poly = stir_whir_gr::poly_utils::interpolate_for_gr_wrapper(
      domain.context(), domain.elements(), evals);
  std::vector<GRElem> expected_coeffs = expected_poly.coefficients();
  expected_coeffs.resize(static_cast<std::size_t>(domain.size()),
                         domain.context().zero());

  for (std::size_t repeat = 0; repeat < repeat_count; ++repeat) {
    const auto actual_evals = stir_whir_gr::poly_utils::fft3(domain, poly);
    CHECK_EQ(actual_evals.size(), expected_evals.size());
    for (std::size_t i = 0; i < actual_evals.size(); ++i) {
      CHECK_EQ(actual_evals[i], expected_evals[i]);
    }

    const auto actual_coeffs = stir_whir_gr::poly_utils::inverse_fft3(domain, evals);
    CHECK_EQ(actual_coeffs.size(), expected_coeffs.size());
    for (std::size_t i = 0; i < actual_coeffs.size(); ++i) {
      CHECK_EQ(actual_coeffs[i], expected_coeffs[i]);
    }
  }
}

void RequireVectorsEqual(const std::vector<GRElem>& actual,
                         const std::vector<GRElem>& expected,
                         const char* label) {
  if (actual.size() != expected.size()) {
    throw std::runtime_error(std::string(label) + " size mismatch");
  }
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (actual[i] != expected[i]) {
      throw std::runtime_error(std::string(label) + " mismatch at index " +
                               std::to_string(i));
    }
  }
}

void TestFft3MatchesRSEncode() {
  testutil::PrintInfo("fft3 matches rs_encode on 1-round and multi-round subgroup/coset domains");

  const GRContext shallow_ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain subgroup3 = Domain::teichmuller_subgroup(shallow_ctx, 3);
  const Domain coset3 =
      Domain::teichmuller_coset(shallow_ctx, shallow_ctx.teich_generator(), 3);
  const Domain subgroup9 = Domain::teichmuller_subgroup(shallow_ctx, 9);
  const Domain coset9 =
      Domain::teichmuller_coset(shallow_ctx, shallow_ctx.teich_generator(), 9);

  const GRContext deep_ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const Domain subgroup27 = Domain::teichmuller_subgroup(deep_ctx, 27);
  const Domain coset27 =
      Domain::teichmuller_coset(deep_ctx, deep_ctx.teich_generator(), 27);

  CheckFftMatchesRSEncode(subgroup3, SamplePolynomial(shallow_ctx, subgroup3, 3));
  CheckFftMatchesRSEncode(coset3, SamplePolynomial(shallow_ctx, coset3, 3));
  CheckFftMatchesRSEncode(subgroup9, SamplePolynomial(shallow_ctx, subgroup9, 5));
  CheckFftMatchesRSEncode(coset9, SamplePolynomial(shallow_ctx, coset9, 5));
  CheckFftMatchesRSEncode(subgroup9, SamplePolynomial(shallow_ctx, subgroup9, 17));
  CheckFftMatchesRSEncode(coset9, SamplePolynomial(shallow_ctx, coset9, 17));
  CheckFftMatchesRSEncode(subgroup27, SamplePolynomial(deep_ctx, subgroup27, 14));
  CheckFftMatchesRSEncode(coset27, SamplePolynomial(deep_ctx, coset27, 14));
}

void TestInverseFft3Roundtrip() {
  testutil::PrintInfo("inverse_fft3 roundtrips coefficients on subgroup and coset");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain subgroup = Domain::teichmuller_subgroup(ctx, 9);
  const Domain coset = Domain::teichmuller_coset(ctx, ctx.teich_generator(), 9);

  CheckInverseRoundtrip(subgroup, SamplePolynomial(ctx, subgroup, 5));
  CheckInverseRoundtrip(coset, SamplePolynomial(ctx, coset, 5));
}

void TestInverseFft3MatchesInterpolationForAliasedInput() {
  testutil::PrintInfo("inverse_fft3 matches rs_interpolate for higher-degree aliased input");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain subgroup = Domain::teichmuller_subgroup(ctx, 9);
  const Domain coset = Domain::teichmuller_coset(ctx, ctx.teich_generator(), 9);

  CheckInverseMatchesInterpolation(subgroup, SamplePolynomial(ctx, subgroup, 17));
  CheckInverseMatchesInterpolation(coset, SamplePolynomial(ctx, coset, 17));
}

void TestInverseFft3RoundtripOnSize27() {
  testutil::PrintInfo(
      "inverse_fft3 handles deeper radix-3 recursion on size-27 subgroup and coset");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const Domain subgroup = Domain::teichmuller_subgroup(ctx, 27);
  const Domain coset = Domain::teichmuller_coset(ctx, ctx.teich_generator(), 27);

  CheckInverseRoundtrip(subgroup, SamplePolynomial(ctx, subgroup, 14));
  CheckInverseRoundtrip(coset, SamplePolynomial(ctx, coset, 14));
}

void TestFft3RejectsNonThreeSmoothDomain() {
  testutil::PrintInfo("fft3 and inverse_fft3 reject domains whose size is not a power of three");

  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain non_three_smooth = Domain::teichmuller_subgroup(ctx, 7);
  const Polynomial poly = SamplePolynomial(ctx, non_three_smooth, 4);

  bool fft_threw = false;
  try {
    (void)stir_whir_gr::poly_utils::fft3(non_three_smooth, poly);
  } catch (const std::invalid_argument&) {
    fft_threw = true;
  }
  CHECK(fft_threw);

  bool ifft_threw = false;
  try {
    (void)stir_whir_gr::poly_utils::inverse_fft3(
        non_three_smooth, std::vector<GRElem>(7, ctx.one()));
  } catch (const std::invalid_argument&) {
    ifft_threw = true;
  }
  CHECK(ifft_threw);
}

void TestFft3RandomDifferential() {
  testutil::PrintInfo("fft3 matches rs_encode across deterministic random trials");

  std::mt19937_64 rng(0xF17D3AULL);

  const GRContext shallow_ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain subgroup9 = Domain::teichmuller_subgroup(shallow_ctx, 9);
  const Domain coset9 =
      Domain::teichmuller_coset(shallow_ctx, shallow_ctx.teich_generator(), 9);

  const GRContext deep_ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const Domain subgroup27 = Domain::teichmuller_subgroup(deep_ctx, 27);
  const Domain coset27 =
      Domain::teichmuller_coset(deep_ctx, deep_ctx.teich_generator(), 27);

  for (int trial = 0; trial < 12; ++trial) {
    const std::size_t size9 = 1U + static_cast<std::size_t>(rng() % 18U);
    const std::size_t size27 = 1U + static_cast<std::size_t>(rng() % 36U);

    CheckFftMatchesRSEncode(subgroup9,
                            SampleRandomPolynomial(shallow_ctx, size9, rng));
    CheckFftMatchesRSEncode(coset9,
                            SampleRandomPolynomial(shallow_ctx, size9, rng));
    CheckFftMatchesRSEncode(subgroup27,
                            SampleRandomPolynomial(deep_ctx, size27, rng));
    CheckFftMatchesRSEncode(coset27,
                            SampleRandomPolynomial(deep_ctx, size27, rng));
  }
}

void TestInverseFft3RandomDifferential() {
  testutil::PrintInfo(
      "inverse_fft3 matches rs_interpolate across deterministic random trials");

  std::mt19937_64 rng(0x1FF73BULL);

  const GRContext shallow_ctx(GRConfig{.p = 2, .k_exp = 16, .r = 6});
  const Domain subgroup9 = Domain::teichmuller_subgroup(shallow_ctx, 9);
  const Domain coset9 =
      Domain::teichmuller_coset(shallow_ctx, shallow_ctx.teich_generator(), 9);

  const GRContext deep_ctx(GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const Domain subgroup27 = Domain::teichmuller_subgroup(deep_ctx, 27);
  const Domain coset27 =
      Domain::teichmuller_coset(deep_ctx, deep_ctx.teich_generator(), 27);

  for (int trial = 0; trial < 12; ++trial) {
    const auto evals9 = SampleRandomEvaluations(shallow_ctx, 9, rng);
    const auto evals27 = SampleRandomEvaluations(deep_ctx, 27, rng);

    CheckInverseMatchesInterpolation(subgroup9, evals9);
    CheckInverseMatchesInterpolation(coset9, evals9);
    CheckInverseMatchesInterpolation(subgroup27, evals27);
    CheckInverseMatchesInterpolation(coset27, evals27);
  }
}

void TestFft3LargeDomainDifferential() {
  testutil::PrintInfo(
      "fft3 and inverse_fft3 match large-domain baselines on size-243 subgroup and coset");

  std::mt19937_64 rng(0x243F7AULL);
  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(ctx, 243);
  const Domain coset243 =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 243);

  const Polynomial poly = SampleRandomPolynomial(ctx, 121, rng);
  const auto evals = SampleRandomEvaluations(ctx, 243, rng);

  CheckFftMatchesRSEncode(subgroup243, poly);
  CheckFftMatchesRSEncode(coset243, poly);
  CheckInverseMatchesInterpolation(subgroup243, evals);
  CheckInverseMatchesInterpolation(coset243, evals);
}

void TestFft3RepeatedDomainCallsStayStableOnSize243() {
  testutil::PrintInfo(
      "fft3 cache miss and subsequent hits stay stable on repeated size-243 subgroup/coset calls");

  std::mt19937_64 rng(0xCACE243ULL);
  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(ctx, 243);
  const Domain coset243 =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 243);

  const Polynomial poly = SampleRandomPolynomial(ctx, 127, rng);
  const auto evals = SampleRandomEvaluations(ctx, 243, rng);

  CheckRepeatedCallsStable(subgroup243, poly, evals, 6U);
  CheckRepeatedCallsStable(coset243, poly, evals, 6U);
}

void TestFft3PlanCachePublishIsSafeUnderConcurrentMiss() {
  testutil::PrintInfo(
      "fft3 plan cache publication is safe under concurrent fresh misses");

  std::mt19937_64 rng(0xC0FFEE243ULL);
  auto shared_ctx = std::make_shared<GRContext>(
      GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(shared_ctx, 243);
  const Polynomial poly = SampleRandomPolynomial(*shared_ctx, 127, rng);
  const auto evals = SampleRandomEvaluations(*shared_ctx, 243, rng);
  const auto expected_evals = NaiveRSEncode(subgroup243, poly);
  const auto expected_poly = stir_whir_gr::poly_utils::interpolate_for_gr_wrapper(
      subgroup243.context(), subgroup243.elements(), evals);
  std::vector<GRElem> expected_coeffs = expected_poly.coefficients();
  expected_coeffs.resize(static_cast<std::size_t>(subgroup243.size()),
                         subgroup243.context().zero());

  constexpr std::size_t kThreadCount = 4;
  constexpr std::size_t kIterationsPerThread = 2;
  std::atomic<bool> start(false);
  std::atomic<bool> failed(false);
  std::mutex error_mutex;
  std::string first_error;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);

  for (std::size_t thread_index = 0; thread_index < kThreadCount;
       ++thread_index) {
    threads.emplace_back([&, thread_index] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      try {
        for (std::size_t iteration = 0; iteration < kIterationsPerThread;
             ++iteration) {
          const bool inverse_first = ((thread_index + iteration) % 2U) != 0U;
          if (inverse_first) {
            RequireVectorsEqual(
                stir_whir_gr::poly_utils::inverse_fft3(subgroup243, evals),
                expected_coeffs, "concurrent inverse_fft3");
            RequireVectorsEqual(stir_whir_gr::poly_utils::fft3(subgroup243, poly),
                                expected_evals, "concurrent fft3");
          } else {
            RequireVectorsEqual(stir_whir_gr::poly_utils::fft3(subgroup243, poly),
                                expected_evals, "concurrent fft3");
            RequireVectorsEqual(
                stir_whir_gr::poly_utils::inverse_fft3(subgroup243, evals),
                expected_coeffs, "concurrent inverse_fft3");
          }
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

void TestFft3SharedContextStabilityOnSize243() {
  testutil::PrintInfo(
      "fft3 and inverse_fft3 remain stable across repeated size-243 calls on a shared GRContext");

  std::mt19937_64 rng(0x5A1E243ULL);
  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(ctx, 243);
  const Domain coset243 =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 243);

  for (int trial = 0; trial < 4; ++trial) {
    const std::size_t term_count =
        81U + static_cast<std::size_t>(rng() % 81U);
    const Polynomial poly = SampleRandomPolynomial(ctx, term_count, rng);
    const auto evals = SampleRandomEvaluations(ctx, 243, rng);

    CheckFftMatchesRSEncode(subgroup243, poly);
    CheckInverseMatchesInterpolation(subgroup243, evals);
    CheckFftMatchesRSEncode(coset243, poly);
    CheckInverseMatchesInterpolation(coset243, evals);
  }
}

void TestFft3SparseResidueClassLayoutsOnSize243() {
  testutil::PrintInfo(
      "fft3 preserves sparse single-residue coefficient layouts on size-243 subgroup and coset");

  std::mt19937_64 rng(0xB9F243ULL);
  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(ctx, 243);
  const Domain coset243 =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 243);

  std::vector<std::size_t> subgroup_indices;
  AppendArithmeticProgressionIndices(&subgroup_indices, 1U, 3U, 243U);
  const Polynomial subgroup_poly =
      MakeSparsePolynomial(ctx, 243U, subgroup_indices, rng);

  std::vector<std::size_t> coset_indices;
  AppendArithmeticProgressionIndices(&coset_indices, 2U, 3U, 243U);
  const Polynomial coset_poly =
      MakeSparsePolynomial(ctx, 243U, coset_indices, rng);

  CheckFftMatchesRSEncode(subgroup243, subgroup_poly);
  CheckInverseRoundtrip(subgroup243, subgroup_poly);
  CheckFftMatchesRSEncode(coset243, coset_poly);
  CheckInverseRoundtrip(coset243, coset_poly);
}

void TestFft3AliasedDeepStrideLayoutsMatchInterpolation() {
  testutil::PrintInfo(
      "fft3 sparse deeper-stride aliased layouts keep forward and inverse semantics aligned");

  std::mt19937_64 rng(0xA11A5E243ULL);
  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(ctx, 243);
  const Domain coset243 =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 243);

  std::vector<std::size_t> sparse_indices;
  AppendArithmeticProgressionIndices(&sparse_indices, 5U, 9U, 486U);
  AppendArithmeticProgressionIndices(&sparse_indices, 20U, 27U, 486U);
  AppendArithmeticProgressionIndices(&sparse_indices, 83U, 81U, 486U);
  const Polynomial poly = MakeSparsePolynomial(ctx, 486U, sparse_indices, rng);

  CheckFftMatchesRSEncode(subgroup243, poly);
  CheckInverseMatchesInterpolation(subgroup243, poly);
  CheckFftMatchesRSEncode(coset243, poly);
  CheckInverseMatchesInterpolation(coset243, poly);
}

void TestFft3NestedTernaryStrideBucketsRoundtrip() {
  testutil::PrintInfo(
      "fft3 preserves nested ternary-stride buckets on size-243 subgroup and coset");

  std::mt19937_64 rng(0x73D1D243ULL);
  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(ctx, 243);
  const Domain coset243 =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 243);

  std::vector<std::size_t> nested_indices;
  AppendArithmeticProgressionIndices(&nested_indices, 7U, 9U, 243U);
  AppendArithmeticProgressionIndices(&nested_indices, 22U, 27U, 243U);
  AppendArithmeticProgressionIndices(&nested_indices, 104U, 81U, 243U);
  AppendArithmeticProgressionIndices(&nested_indices, 161U, 243U, 243U);
  const Polynomial poly = MakeSparsePolynomial(ctx, 243U, nested_indices, rng);

  CheckFftMatchesRSEncode(subgroup243, poly);
  CheckInverseRoundtrip(subgroup243, poly);
  CheckFftMatchesRSEncode(coset243, poly);
  CheckInverseRoundtrip(coset243, poly);
}

void TestFft3AliasedInterleavedStrideLayoutsMatchInterpolation() {
  testutil::PrintInfo(
      "fft3 interleaved aliased stride layouts keep subgroup and coset semantics identical");

  std::mt19937_64 rng(0xA11A5ED9ULL);
  const GRContext ctx(GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const Domain subgroup243 = Domain::teichmuller_subgroup(ctx, 243);
  const Domain coset243 =
      Domain::teichmuller_coset(ctx, ctx.teich_generator(), 243);

  std::vector<std::size_t> sparse_indices;
  AppendArithmeticProgressionIndices(&sparse_indices, 2U, 9U, 729U);
  AppendArithmeticProgressionIndices(&sparse_indices, 34U, 27U, 729U);
  AppendArithmeticProgressionIndices(&sparse_indices, 123U, 81U, 729U);
  AppendArithmeticProgressionIndices(&sparse_indices, 364U, 243U, 729U);
  const Polynomial poly = MakeSparsePolynomial(ctx, 729U, sparse_indices, rng);

  CheckFftMatchesRSEncode(subgroup243, poly);
  CheckInverseMatchesInterpolation(subgroup243, poly);
  CheckFftMatchesRSEncode(coset243, poly);
  CheckInverseMatchesInterpolation(coset243, poly);
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestFft3MatchesRSEncode);
    RUN_TEST(TestInverseFft3Roundtrip);
    RUN_TEST(TestInverseFft3MatchesInterpolationForAliasedInput);
    RUN_TEST(TestInverseFft3RoundtripOnSize27);
    RUN_TEST(TestFft3RejectsNonThreeSmoothDomain);
    RUN_TEST(TestFft3RandomDifferential);
    RUN_TEST(TestInverseFft3RandomDifferential);
    RUN_TEST(TestFft3LargeDomainDifferential);
    RUN_TEST(TestFft3RepeatedDomainCallsStayStableOnSize243);
    RUN_TEST(TestFft3PlanCachePublishIsSafeUnderConcurrentMiss);
    RUN_TEST(TestFft3SharedContextStabilityOnSize243);
    RUN_TEST(TestFft3SparseResidueClassLayoutsOnSize243);
    RUN_TEST(TestFft3AliasedDeepStrideLayoutsMatchInterpolation);
    RUN_TEST(TestFft3NestedTernaryStrideBucketsRoundtrip);
    RUN_TEST(TestFft3AliasedInterleavedStrideLayoutsMatchInterpolation);
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
