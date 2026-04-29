#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "tests/test_common.hpp"
#include "whir/soundness.hpp"

int g_failures = 0;

namespace {

std::uint64_t Pow3(std::uint64_t exponent) {
  std::uint64_t result = 1;
  for (std::uint64_t i = 0; i < exponent; ++i) {
    result *= 3;
  }
  return result;
}

template <typename Fn> void ExpectInvalidArgument(Fn fn) {
  bool threw = false;
  try {
    fn();
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}

swgr::whir::WhirUniqueDecodingInputs SmallInputs() {
  return swgr::whir::WhirUniqueDecodingInputs{
      .lambda_target = 32,
      .ring_exponent = 16,
      .variable_count = 3,
      .max_layer_width = 1,
      .rho0 = swgr::whir::WhirRational{1, 3},
      .theta = swgr::whir::WhirRational{1, 2},
  };
}

void TestInvalidRatesReject() {
  testutil::PrintInfo("WHIR selector rejects invalid rho0 and theta");

  auto inputs = SmallInputs();
  inputs.rho0 = swgr::whir::WhirRational{0, 1};
  ExpectInvalidArgument(
      [&] { swgr::whir::select_whir_unique_decoding_parameters(inputs); });

  inputs = SmallInputs();
  inputs.rho0 = swgr::whir::WhirRational{1, 1};
  ExpectInvalidArgument(
      [&] { swgr::whir::select_whir_unique_decoding_parameters(inputs); });

  inputs = SmallInputs();
  inputs.theta = swgr::whir::WhirRational{0, 1};
  ExpectInvalidArgument(
      [&] { swgr::whir::select_whir_unique_decoding_parameters(inputs); });

  inputs = SmallInputs();
  inputs.theta = swgr::whir::WhirRational{1, 1};
  ExpectInvalidArgument(
      [&] { swgr::whir::select_whir_unique_decoding_parameters(inputs); });
}

void TestSmallSmokeParamsAreAccepted() {
  testutil::PrintInfo("WHIR selector accepts small unique-decoding params");

  const auto selected =
      swgr::whir::select_whir_unique_decoding_parameters(SmallInputs());

  CHECK(selected.feasible);
  CHECK_EQ(selected.public_params.base_prime, std::uint64_t{2});
  CHECK_EQ(selected.public_params.ring_exponent, std::uint64_t{16});
  CHECK_EQ(selected.public_params.initial_domain_size, std::uint64_t{81});
  CHECK_EQ(selected.public_params.layer_widths.size(), std::size_t{3});
  CHECK_EQ(selected.public_params.shift_repetitions.size(), std::size_t{3});
  CHECK_EQ(selected.public_params.final_repetitions, std::uint64_t{0});
  CHECK_EQ(selected.repetition_security_bits, std::uint64_t{35});
  CHECK(selected.effective_security_bits >= std::uint64_t{32});
}

void TestSelectedN0DividesSelectedTeichmullerGroup() {
  testutil::PrintInfo("WHIR selected n0 divides 2^r - 1");

  const auto selected =
      swgr::whir::select_whir_unique_decoding_parameters(SmallInputs());

  CHECK(selected.rdom != 0);
  CHECK_EQ(selected.selected_r % selected.rdom, std::uint64_t{0});
  CHECK(swgr::whir::domain_divides_teichmuller_group(
      selected.public_params.initial_domain_size, selected.selected_r));
  CHECK_EQ(swgr::whir::multiplicative_order_mod_odd(
               selected.public_params.initial_domain_size, 2),
           selected.rdom);
}

void TestRequiredDomainsDivideThroughRoundChain() {
  testutil::PrintInfo("WHIR layer and shift domains divide through the chain");

  auto inputs = SmallInputs();
  inputs.variable_count = 4;
  inputs.max_layer_width = 2;
  const auto selected =
      swgr::whir::select_whir_unique_decoding_parameters(inputs);

  CHECK(selected.feasible);
  CHECK_EQ(selected.required_3_adic_power, std::uint64_t{3});
  CHECK_EQ(selected.public_params.initial_domain_size, std::uint64_t{243});
  CHECK_EQ(selected.layers.size(), std::size_t{2});
  CHECK_EQ(selected.layers[0].width, std::uint64_t{2});
  CHECK_EQ(selected.layers[1].width, std::uint64_t{2});

  for (const auto &layer : selected.layers) {
    CHECK_EQ(layer.domain_size % Pow3(layer.width), std::uint64_t{0});
    CHECK(layer.rate > 0.0L);
    CHECK(layer.rate < 1.0L);
    CHECK(layer.delta > 0.0L);
    CHECK(layer.delta < 0.5L * (1.0L - layer.rate));
  }
}

void TestAlgebraicBoundIsBelowTarget() {
  testutil::PrintInfo("WHIR algebraic error bound is below 2^-lambda");

  const auto inputs = SmallInputs();
  const auto selected =
      swgr::whir::select_whir_unique_decoding_parameters(inputs);

  CHECK(selected.feasible);
  CHECK(selected.algebraic_error_log2 <
        -static_cast<long double>(inputs.lambda_target));
  CHECK(selected.total_error_log2 <=
        -static_cast<long double>(inputs.lambda_target));
  CHECK(selected.effective_security_bits >= inputs.lambda_target);
}

void TestDomainGuardCanReportInfeasible() {
  testutil::PrintInfo("WHIR selector reports infeasible benchmark guards");

  auto inputs = SmallInputs();
  inputs.max_domain_size = 27;
  const auto selected =
      swgr::whir::select_whir_unique_decoding_parameters(inputs);

  CHECK(!selected.feasible);
  bool saw_guard_note = false;
  for (const auto &note : selected.notes) {
    if (note.find("max_domain_size") != std::string::npos) {
      saw_guard_note = true;
    }
  }
  CHECK(saw_guard_note);
}

void TestExtensionGuardCanReportInfeasible() {
  testutil::PrintInfo("WHIR selector honors the max-r benchmark guard");

  auto inputs = SmallInputs();
  inputs.max_extension_degree = 1;
  inputs.max_n0_search_steps = 2;
  const auto selected =
      swgr::whir::select_whir_unique_decoding_parameters(inputs);

  CHECK(!selected.feasible);
  bool saw_guard_note = false;
  for (const auto &note : selected.notes) {
    if (note.find("max_extension_degree") != std::string::npos) {
      saw_guard_note = true;
    }
  }
  CHECK(saw_guard_note);
}

} // namespace

int main() {
  RUN_TEST(TestInvalidRatesReject);
  RUN_TEST(TestSmallSmokeParamsAreAccepted);
  RUN_TEST(TestSelectedN0DividesSelectedTeichmullerGroup);
  RUN_TEST(TestRequiredDomainsDivideThroughRoundChain);
  RUN_TEST(TestAlgebraicBoundIsBelowTarget);
  RUN_TEST(TestDomainGuardCanReportInfeasible);
  RUN_TEST(TestExtensionGuardCanReportInfeasible);

  if (g_failures != 0) {
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "All WHIR soundness tests passed\n";
  return 0;
}
