#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "NTL/ZZ_pE.h"

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "tests/test_common.hpp"
#include "whir/common.hpp"
#include "whir/multiquadratic.hpp"
#include "whir/prover.hpp"
#include "whir/verifier.hpp"

int g_failures = 0;

namespace {

using stir_whir_gr::algebra::GRConfig;
using stir_whir_gr::algebra::GRContext;
using stir_whir_gr::algebra::GRElem;

struct RoundtripCase {
  const char* name = "";
  std::uint64_t variable_count = 0;
  std::uint64_t max_layer_width = 1;
};

GRElem SmallElement(std::uint64_t value) {
  GRElem out;
  NTL::clear(out);
  GRElem one;
  NTL::set(one);
  for (std::uint64_t i = 0; i < value; ++i) {
    out += one;
  }
  return out;
}

std::uint64_t ExtensionDegreeForDomainSize(std::uint64_t domain_size) {
  if (domain_size == 9) {
    return 6;
  }
  if (domain_size == 27) {
    return 18;
  }
  if (domain_size == 81) {
    return 54;
  }
  throw std::invalid_argument("unsupported WHIR roundtrip domain size");
}

std::vector<std::uint64_t> LayerWidths(std::uint64_t variable_count,
                                       std::uint64_t max_layer_width) {
  std::vector<std::uint64_t> widths;
  std::uint64_t remaining = variable_count;
  while (remaining != 0) {
    const std::uint64_t width = std::min(max_layer_width, remaining);
    widths.push_back(width);
    remaining -= width;
  }
  return widths;
}

stir_whir_gr::whir::WhirPublicParameters BuildPublicParameters(
    std::uint64_t variable_count, std::uint64_t max_layer_width) {
  const std::uint64_t domain_size =
      stir_whir_gr::whir::pow3_checked(variable_count + 1U);
  auto ctx = std::make_shared<GRContext>(
      GRConfig{.p = 2,
               .k_exp = 16,
               .r = ExtensionDegreeForDomainSize(domain_size)});
  const stir_whir_gr::Domain domain =
      stir_whir_gr::Domain::teichmuller_subgroup(ctx, domain_size);
  return ctx->with_ntl_context([&] {
    const GRElem omega =
        NTL::power(domain.root(), static_cast<long>(domain_size / 3U));
    const auto widths = LayerWidths(variable_count, max_layer_width);
    return stir_whir_gr::whir::WhirPublicParameters{
        .ctx = ctx,
        .initial_domain = domain,
        .variable_count = variable_count,
        .layer_widths = widths,
        .shift_repetitions = std::vector<std::uint64_t>(widths.size(), 1),
        .final_repetitions = 1,
        .degree_bounds = std::vector<std::uint64_t>(widths.size(), 4),
        .deltas = std::vector<long double>(widths.size(), 0.1L),
        .omega = omega,
        .ternary_grid = {ctx->one(), omega, omega * omega},
        .lambda_target = 32,
        .hash_profile = stir_whir_gr::HashProfile::WHIR_NATIVE,
    };
  });
}

stir_whir_gr::whir::MultiQuadraticPolynomial BuildPolynomial(
    const GRContext& ctx, std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> coefficients;
    coefficients.reserve(
        static_cast<std::size_t>(stir_whir_gr::whir::pow3_checked(variable_count)));
    for (std::uint64_t i = 0; i < stir_whir_gr::whir::pow3_checked(variable_count);
         ++i) {
      coefficients.push_back(SmallElement((11U * i + 5U) % 23U));
    }
    return stir_whir_gr::whir::MultiQuadraticPolynomial(variable_count,
                                                std::move(coefficients));
  });
}

std::vector<GRElem> BuildOpenPoint(const GRContext& ctx,
                                   std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<GRElem> point;
    point.reserve(static_cast<std::size_t>(variable_count));
    for (std::uint64_t i = 0; i < variable_count; ++i) {
      point.push_back(SmallElement(7U + 3U * i));
    }
    return point;
  });
}

void RunRoundtripCase(const RoundtripCase& test_case) {
  testutil::PrintInfo(std::string("WHIR honest roundtrip ") + test_case.name);

  const auto pp = BuildPublicParameters(test_case.variable_count,
                                        test_case.max_layer_width);
  stir_whir_gr::whir::WhirParameters params;
  params.lambda_target = pp.lambda_target;
  params.hash_profile = pp.hash_profile;
  const auto polynomial = BuildPolynomial(*pp.ctx, test_case.variable_count);
  const auto point = BuildOpenPoint(*pp.ctx, test_case.variable_count);

  const stir_whir_gr::whir::WhirProver prover(params);
  const stir_whir_gr::whir::WhirVerifier verifier(params);
  stir_whir_gr::whir::WhirCommitmentState state;
  const auto commitment = prover.commit(pp, polynomial, &state);
  const auto opening = prover.open(commitment, state, point);

  stir_whir_gr::ProofStatistics verifier_stats;
  CHECK(verifier.verify(commitment, point, opening, &verifier_stats));
  CHECK_EQ(opening.value, polynomial.evaluate(*pp.ctx, point));
  CHECK_EQ(opening.proof.rounds.size(), pp.layer_widths.size());
  for (std::size_t i = 0; i < pp.layer_widths.size(); ++i) {
    CHECK_EQ(opening.proof.rounds[i].sumcheck_polynomials.size(),
             static_cast<std::size_t>(pp.layer_widths[i]));
  }
  CHECK_EQ(verifier_stats.serialized_bytes,
           stir_whir_gr::whir::serialized_message_bytes(*pp.ctx, opening));
  CHECK(commitment.stats.prover_encode_ms > 0.0);
  CHECK(commitment.stats.prover_merkle_ms > 0.0);
  CHECK(opening.proof.stats.prover_encode_ms > 0.0);
  CHECK(opening.proof.stats.prover_merkle_ms > 0.0);
  CHECK(opening.proof.stats.prover_transcript_ms > 0.0);
  CHECK(opening.proof.stats.prover_fold_ms > 0.0);
  CHECK(opening.proof.stats.prover_interpolate_ms > 0.0);
  CHECK(opening.proof.stats.prover_query_open_ms > 0.0);
  CHECK(verifier_stats.verifier_merkle_ms > 0.0);
  CHECK(verifier_stats.verifier_transcript_ms > 0.0);
  CHECK(verifier_stats.verifier_query_phase_ms > 0.0);
  CHECK(verifier_stats.verifier_algebra_ms > 0.0);
}

void TestRoundtripsCoverCompletionCriteria() {
  const std::vector<RoundtripCase> cases{
      {.name = "m=1,b=1", .variable_count = 1, .max_layer_width = 1},
      {.name = "m=2,b=1", .variable_count = 2, .max_layer_width = 1},
      {.name = "m=3,b=1", .variable_count = 3, .max_layer_width = 1},
      {.name = "m=2,b=2", .variable_count = 2, .max_layer_width = 2},
  };
  for (const auto& test_case : cases) {
    RunRoundtripCase(test_case);
  }
}

}  // namespace

int main() {
  try {
    RUN_TEST(TestRoundtripsCoverCompletionCriteria);
  } catch (const std::exception& ex) {
    std::cerr << "Unhandled std::exception: " << ex.what() << "\n";
    return 2;
  } catch (...) {
    std::cerr << "Unhandled non-std exception\n";
    return 2;
  }

  if (g_failures == 0) {
    std::cout << "\nAll WHIR roundtrip tests passed.\n";
    return 0;
  }

  std::cerr << "\n" << g_failures << " test(s) failed.\n";
  return 1;
}
