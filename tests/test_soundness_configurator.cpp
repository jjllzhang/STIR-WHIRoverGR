#include <iostream>
#include <string>

#include "soundness/configurator.hpp"
#include "tests/test_common.hpp"

int g_failures = 0;

namespace {

void TestManualQueryValidationRejectsZero() {
  testutil::PrintInfo(
      "soundness configurator rejects zero entries in manual schedules");

  CHECK(stir_whir_gr::soundness::validate_manual_queries({2, 1}));
  CHECK(!stir_whir_gr::soundness::validate_manual_queries({2, 0}));
}

void TestAutoHeuristicMatchesCurrentRoundShape() {
  testutil::PrintInfo(
      "soundness configurator keeps the current conjecture-capacity round shape");

  CHECK_EQ(stir_whir_gr::soundness::auto_query_count_for_round(
               stir_whir_gr::SecurityMode::ConjectureCapacity, 64, 0, 1.0, 0U),
           std::uint64_t{2});
  CHECK_EQ(stir_whir_gr::soundness::auto_query_count_for_round(
               stir_whir_gr::SecurityMode::ConjectureCapacity, 64, 0, 1.0, 1U),
           std::uint64_t{1});
  CHECK_EQ(stir_whir_gr::soundness::auto_query_count_for_round(
               stir_whir_gr::SecurityMode::Conservative, 128, 22, 1.0, 0U),
           std::uint64_t{3});
}

void TestEngineeringHeuristicResultCarriesPolicyAndCaveat() {
  testutil::PrintInfo(
      "soundness configurator reports engineering metadata and caveats");

  const auto result = stir_whir_gr::soundness::engineering_heuristic_result(
      stir_whir_gr::SecurityMode::Conservative, 128, 22, true, 1.0);

  CHECK_EQ(result.model, std::string("engineering-heuristic-v1"));
  CHECK_EQ(result.scope, std::string("engineering_metadata_non_paper"));
  CHECK_EQ(result.query_policy, std::string("manual"));
  CHECK_EQ(result.pow_policy, std::string("fixed_bits"));
  CHECK_EQ(result.effective_security_bits, std::uint64_t{106});

  bool saw_formula_note = false;
  bool saw_non_theorem_note = false;
  bool saw_gr_caveat = false;
  for (const auto& note : result.notes) {
    if (note.find("Engineering heuristic only") != std::string::npos) {
      saw_formula_note = true;
    }
    if (note.find("not theorem-level or paper-complete security claims") !=
        std::string::npos) {
      saw_non_theorem_note = true;
    }
    if (note.find("degree-correction soundness is not yet fully formalized") !=
        std::string::npos) {
      saw_gr_caveat = true;
    }
  }
  CHECK(saw_formula_note);
  CHECK(saw_non_theorem_note);
  CHECK(saw_gr_caveat);
}

}  // namespace

int main() {
  RUN_TEST(TestManualQueryValidationRejectsZero);
  RUN_TEST(TestAutoHeuristicMatchesCurrentRoundShape);
  RUN_TEST(TestEngineeringHeuristicResultCarriesPolicyAndCaveat);

  if (g_failures != 0) {
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "All soundness configurator tests passed\n";
  return 0;
}
