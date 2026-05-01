#include "bench_common.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <NTL/ZZ_pE.h>

#if defined(SWGR_HAS_OPENMP)
#include <omp.h>
#endif

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "fri/prover.hpp"
#include "fri/soundness.hpp"
#include "fri/verifier.hpp"
#include "ldt.hpp"
#include "poly_utils/polynomial.hpp"
#include "stir/prover.hpp"
#include "stir/soundness.hpp"
#include "stir/verifier.hpp"
#include "whir/prover.hpp"
#include "whir/soundness.hpp"
#include "whir/verifier.hpp"

namespace {

enum class FriSoundnessMode {
  TheoremAuto,
  ManualRepetition,
};

enum class WhirPolynomialKind {
  MultiQuadratic,
  Multilinear,
};

struct TimeBenchOptions {
  std::vector<std::string> protocols = {"fri3", "fri9", "stir9to3"};
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 162;
  bool r_was_set = false;
  std::uint64_t n = 243;
  std::uint64_t d = 81;
  FriSoundnessMode fri_soundness_mode = FriSoundnessMode::TheoremAuto;
  std::optional<std::uint64_t> fri_repetitions;
  std::uint64_t lambda_target = 128;
  std::uint64_t pow_bits = 0;
  swgr::SecurityMode sec_mode = swgr::SecurityMode::ConjectureCapacity;
  swgr::HashProfile hash_profile = swgr::HashProfile::STIR_NATIVE;
  std::uint64_t stop_degree = 9;
  std::uint64_t ood_samples = 2;
  std::vector<std::uint64_t> queries;
  bool stir_query_theorem_auto = false;
  std::uint64_t whir_m = 3;
  std::uint64_t whir_bmax = 1;
  swgr::whir::WhirRational whir_rho0{1, 3};
  WhirPolynomialKind whir_polynomial_kind = WhirPolynomialKind::MultiQuadratic;
  std::optional<std::uint64_t> whir_fixed_r;
  std::optional<std::uint64_t> whir_repetitions;
  std::uint64_t threads = 1;
  std::uint64_t warmup = 1;
  std::uint64_t reps = 3;
  swgr::bench::OutputFormat format = swgr::bench::OutputFormat::Csv;
};

struct TimeBenchRow {
  std::string protocol;
  std::string ring;
  std::uint64_t n = 0;
  std::uint64_t d = 0;
  std::string rho;
  std::string soundness_mode;
  std::uint64_t fri_repetitions = 0;
  std::uint64_t lambda_target = 0;
  std::uint64_t pow_bits = 0;
  std::string sec_mode;
  std::string hash_profile;
  std::string soundness_model;
  std::string soundness_scope;
  std::string query_policy;
  std::string pow_policy;
  std::uint64_t effective_security_bits = 0;
  std::string soundness_notes;
  std::uint64_t fold = 0;
  std::uint64_t shift_power = 0;
  std::uint64_t stop_degree = 0;
  std::uint64_t ood_samples = 0;
  std::uint64_t threads = 0;
  std::uint64_t warmup = 0;
  std::uint64_t reps = 0;
  double commit_ms = 0.0;
  double prove_query_phase_ms = 0.0;
  double prover_total_ms = 0.0;
  double verify_ms = 0.0;
  std::uint64_t serialized_bytes_actual = 0;
  double serialized_kib_actual = 0.0;
  std::uint64_t verifier_hashes_actual = 0;
  double profile_prover_total_ms = 0.0;
  double profile_prover_encode_total_ms = 0.0;
  double profile_prover_merkle_total_ms = 0.0;
  double profile_prover_transcript_total_ms = 0.0;
  double profile_prover_fold_total_ms = 0.0;
  double profile_prover_interpolate_total_ms = 0.0;
  double profile_prover_query_open_total_ms = 0.0;
  double profile_prover_ood_total_ms = 0.0;
  double profile_prover_answer_total_ms = 0.0;
  double profile_prover_quotient_total_ms = 0.0;
  double profile_prover_degree_correction_total_ms = 0.0;
  double profile_verify_total_ms = 0.0;
  double profile_verify_merkle_total_ms = 0.0;
  double profile_verify_transcript_total_ms = 0.0;
  double profile_verify_query_total_ms = 0.0;
  double profile_verify_algebra_total_ms = 0.0;
};

struct ResolvedFriSoundness {
  FriSoundnessMode mode = FriSoundnessMode::TheoremAuto;
  std::uint64_t repetition_count = 0;
  std::uint64_t lambda_target = 0;
  bool explicit_repetition_override = false;
  std::optional<swgr::fri::StandaloneFriSoundnessAnalysis> theorem_analysis;
};

double SafeMean(double total, std::uint64_t reps) {
  return reps == 0 ? 0.0 : total / static_cast<double>(reps);
}

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
      .count();
}

double ClampNonNegative(double value) {
  return value < 0.0 ? 0.0 : value;
}

std::string JoinNotes(const std::vector<std::string>& notes) {
  std::string joined;
  for (std::size_t index = 0; index < notes.size(); ++index) {
    if (!joined.empty()) {
      joined += " | ";
    }
    joined += notes[index];
  }
  return joined;
}

FriSoundnessMode ParseFriSoundnessMode(const std::string& value) {
  if (value == "theorem_auto") {
    return FriSoundnessMode::TheoremAuto;
  }
  if (value == "manual_repetition") {
    return FriSoundnessMode::ManualRepetition;
  }
  throw std::invalid_argument(
      "unsupported --fri-soundness-mode: " + value +
      " (expected theorem_auto or manual_repetition)");
}

WhirPolynomialKind ParseWhirPolynomialKind(const std::string& value) {
  const std::string lowered = swgr::bench::ToLowerCopy(value);
  if (lowered == "multiquadratic" || lowered == "multi_quadratic") {
    return WhirPolynomialKind::MultiQuadratic;
  }
  if (lowered == "multilinear" || lowered == "multi_linear") {
    return WhirPolynomialKind::Multilinear;
  }
  throw std::invalid_argument(
      "unsupported --whir-polynomial: " + value +
      " (expected multiquadratic or multilinear)");
}

std::string DeltaRatioString(
    const swgr::fri::StandaloneFriSoundnessAnalysis& analysis) {
  return std::to_string(analysis.delta_numerator) + "/" +
         std::to_string(analysis.delta_denominator);
}

swgr::whir::WhirRational ParseWhirRational(std::string_view flag_name,
                                           std::string_view raw_value) {
  const std::string owned(raw_value);
  const std::size_t slash = owned.find('/');
  if (slash == std::string::npos) {
    return swgr::whir::WhirRational{
        swgr::bench::ParseUint64(flag_name, owned), 1};
  }
  const auto numerator =
      swgr::bench::ParseUint64(flag_name, owned.substr(0, slash));
  const auto denominator =
      swgr::bench::ParseUint64(flag_name, owned.substr(slash + 1));
  return swgr::whir::WhirRational{numerator, denominator};
}

ResolvedFriSoundness ResolveFriSoundness(const TimeBenchOptions& options,
                                         std::uint64_t fold_factor) {
  ResolvedFriSoundness resolved;
  resolved.mode = options.fri_soundness_mode;
  resolved.explicit_repetition_override = options.fri_repetitions.has_value();

  if (options.fri_soundness_mode == FriSoundnessMode::ManualRepetition) {
    if (!options.fri_repetitions.has_value()) {
      throw std::invalid_argument(
          "--fri-repetitions must be provided when "
          "--fri-soundness-mode=manual_repetition");
    }
    resolved.repetition_count = *options.fri_repetitions;
    return resolved;
  }

  const auto analysis = swgr::fri::analyze_standalone_soundness(
      swgr::fri::StandaloneFriSoundnessInputs{
          .base_prime = options.p,
          .ring_extension_degree = options.r,
          .domain_size = options.n,
          .fold_factor = fold_factor,
          // The quotient oracle bound is d - 1, so the corresponding code
          // dimension for the standalone FRI PCS soundness bound is d.
          .quotient_code_dimension = options.d,
          .lambda_target = options.lambda_target,
      });
  if (!analysis.span_term_within_target) {
    throw std::invalid_argument(
        "standalone FRI PCS theorem_auto is impossible for this parameter set: "
        "s*ell/2^r already exceeds 2^-lambda_target");
  }

  resolved.lambda_target = options.lambda_target;
  resolved.repetition_count = analysis.minimum_repetition_count;
  resolved.theorem_analysis = analysis;
  if (options.fri_repetitions.has_value()) {
    if (*options.fri_repetitions < resolved.repetition_count) {
      std::ostringstream message;
      message << "explicit --fri-repetitions=" << *options.fri_repetitions
              << " is smaller than the theorem-required minimum m="
              << resolved.repetition_count
              << " for standalone FRI PCS theorem_auto";
      throw std::invalid_argument(message.str());
    }
    resolved.repetition_count = *options.fri_repetitions;
  }
  return resolved;
}

void ApplyThreadControl(std::uint64_t requested_threads) {
#if defined(SWGR_HAS_OPENMP)
  int requested = std::numeric_limits<int>::max();
  if (requested_threads <=
      static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    requested = static_cast<int>(requested_threads);
  } else {
    std::cerr << "warning: --threads=" << requested_threads
              << " exceeds OpenMP int limit; capping to " << requested << "\n";
  }

  omp_set_dynamic(0);
  omp_set_num_threads(requested);
  const int effective_threads = omp_get_max_threads();
  std::cerr << "info: OpenMP thread control enabled (requested="
            << requested_threads << ", effective=" << effective_threads << ")\n";
#else
  (void)requested_threads;
  std::cerr
      << "warning: OpenMP is disabled in this build; --threads cannot affect runtime\n";
#endif
}

void FillFriSoundnessMetadata(TimeBenchRow& row,
                              const ResolvedFriSoundness& resolved) {
  row.fri_repetitions = resolved.repetition_count;
  row.pow_bits = 0;
  row.sec_mode.clear();
  row.query_policy = "repeat_steps_3_4_5";
  row.pow_policy = "not_applicable";

  if (resolved.mode == FriSoundnessMode::ManualRepetition) {
    row.soundness_mode = "manual_standalone_fri";
    row.lambda_target = 0;
    row.soundness_model = "manual_repetition_count_m";
    row.soundness_scope = "non_theorem_manual_repetition";
    row.effective_security_bits = 0;
    row.soundness_notes =
        "standalone FRI PCS uses caller-provided repetition count m without "
        "theorem-level lambda solving; use theorem_auto for paper-aligned "
        "parameterization.";
    return;
  }

  if (!resolved.theorem_analysis.has_value()) {
    throw std::invalid_argument(
        "theorem_fri metadata requires theorem analysis");
  }

  row.soundness_mode = "theorem_fri";
  row.lambda_target = resolved.lambda_target;
  row.soundness_model = "epsilon_rbr_fri_max_span_and_repetition";
  row.soundness_scope = "paper_parameterization_lambda_target";
  row.effective_security_bits = resolved.lambda_target;
  std::ostringstream notes;
  notes << "standalone FRI PCS solved m from max(s*ell/2^r,(1-delta)^m)"
        << "<=2^-lambda with delta="
        << DeltaRatioString(*resolved.theorem_analysis)
        << ", required_m=" << resolved.theorem_analysis->minimum_repetition_count;
  if (resolved.explicit_repetition_override &&
      resolved.repetition_count !=
          resolved.theorem_analysis->minimum_repetition_count) {
    notes << ", using explicit m=" << resolved.repetition_count
          << " because it still meets the theorem target";
  }
  row.soundness_notes = notes.str();
}

void FillStirTheoremSoundnessMetadata(
    TimeBenchRow& row, const swgr::stir::StirTheoremSoundnessAnalysis& analysis,
    swgr::SecurityMode sec_mode, std::uint64_t lambda_target,
    std::uint64_t pow_bits, std::string_view query_policy) {
  row.soundness_mode = "theorem_gr";
  row.fri_repetitions = 0;
  row.lambda_target = lambda_target;
  row.pow_bits = pow_bits;
  row.sec_mode = swgr::to_string(sec_mode);
  row.soundness_model = "epsilon_rbr_stir_gr_half_gap_unique_ood";
  row.soundness_scope = "theorem_gr_existing_z2ksnark_half_gap_results";
  row.query_policy = std::string(query_policy);
  row.pow_policy = "benchmark_only_not_in_theorem_bound";
  row.effective_security_bits =
      analysis.feasible ? analysis.effective_security_bits : 0;

  std::vector<std::string> notes{
      "unique-decoding OOD over the explicit exceptional complement",
      "folding and comb challenges are sampled from Teichmuller T",
      "half-gap GR proximity assumptions come from existing Z2KSNARK results",
      "pow_bits is retained as a live benchmark/query knob and is not subtracted from theorem security bits",
  };
  if (query_policy == "manual_live_schedule") {
    notes.push_back(
        "manual query schedule overrides the live auto schedule before theorem analysis");
  } else if (query_policy == "theorem_auto_solved_schedule") {
    notes.push_back(
        "query schedule was solved from lambda_target using the current theorem_gr half-gap model");
  }
  if (!analysis.feasible) {
    notes.push_back(
        "theorem analysis marked this parameter set unsupported for theorem_gr half-gap reporting");
  }
  notes.insert(notes.end(), analysis.assumptions.begin(),
               analysis.assumptions.end());
  row.soundness_notes = JoinNotes(notes);
}

double ProverProfileAccountedTotal(const TimeBenchRow& row) {
  return row.profile_prover_encode_total_ms +
         row.profile_prover_merkle_total_ms +
         row.profile_prover_transcript_total_ms +
         row.profile_prover_fold_total_ms +
         row.profile_prover_interpolate_total_ms +
         row.profile_prover_query_open_total_ms +
         row.profile_prover_ood_total_ms +
         row.profile_prover_answer_total_ms +
         row.profile_prover_quotient_total_ms +
         row.profile_prover_degree_correction_total_ms;
}

double ProverProfileUnaccountedTotal(const TimeBenchRow& row) {
  return ClampNonNegative(row.profile_prover_total_ms -
                          ProverProfileAccountedTotal(row));
}

double VerifyProfileAccountedTotal(const TimeBenchRow& row) {
  return row.profile_verify_merkle_total_ms +
         row.profile_verify_transcript_total_ms +
         row.profile_verify_query_total_ms +
         row.profile_verify_algebra_total_ms;
}

double VerifyProfileUnaccountedTotal(const TimeBenchRow& row) {
  return ClampNonNegative(row.profile_verify_total_ms -
                          VerifyProfileAccountedTotal(row));
}

std::string TimeBenchUsage(const char* binary_name) {
  return std::string("Usage: ") + binary_name +
         " [options]\n"
         "  --protocol fri3|fri9|stir9to3|whir_gr_ud|all\n"
         "  --p <uint> --k-exp <uint> --r <uint>\n"
         "  --n <uint> --d <uint>\n"
         "  --fri-soundness-mode theorem_auto|manual_repetition\n"
         "  --fri-repetitions <uint>\n"
         "  --lambda <uint> --pow-bits <uint>\n"
         "  --sec-mode ConjectureCapacity|Conservative\n"
         "  --hash-profile STIR_NATIVE|WHIR_NATIVE\n"
         "  --stop-degree <uint> --ood-samples <uint>\n"
         "  --queries auto|theorem_auto|q0[,q1,...] --threads <uint>\n"
         "  --whir-m <uint> --whir-bmax <uint>\n"
         "  --whir-r <uint> or --whir-fixed-r <uint>\n"
         "  --whir-polynomial multiquadratic|multilinear\n"
         "  --whir-rho0 <num/den>\n"
         "  --whir-repetitions <uint>\n"
         "    note: fri3/fri9 default to theorem_auto, which solves the\n"
         "    theorem-facing standalone FRI PCS repetition count m from\n"
         "    lambda_target; an explicit --fri-repetitions must then be >=\n"
         "    the required minimum m\n"
         "    note: theorem_auto currently follows the Z2K-style p=2 bound and\n"
         "    also requires delta > 0, so very high-rate toy domains may need\n"
         "    manual_repetition instead\n"
         "    note: manual_repetition keeps a caller-provided m for FRI rows,\n"
         "    but those rows are intentionally non-theorem metadata\n"
         "    note: STIR keeps three query modes: auto for the legacy\n"
         "    heuristic schedule, theorem_auto for the theorem_gr query\n"
         "    solver, and q0[,q1,...] for an explicit manual schedule\n"
         "    note: theorem metadata is reported for the resulting live\n"
         "    parameterization, and pow_bits is not folded into theorem\n"
         "    security bits\n"
         "    note: --protocol all expands to fri3,fri9,stir9to3,whir_gr_ud\n"
         "    note: whir_gr_ud runs the unique-decoding GR(2^s,r) WHIR PCS\n"
         "    selector from whir_gr2k_pcs.pdf; --k-exp supplies s, and r is\n"
         "    chosen from lambda unless --whir-r/--whir-fixed-r or explicit --r\n"
         "    fixes it first\n"
         "    note: text/csv/json output currently keeps one shared row schema\n"
         "    across FRI, STIR, and WHIR; theorem_fri and theorem_gr\n"
         "    rows use lambda_target/effective_security_bits, while\n"
         "    manual_standalone_fri rows still leave theorem-only fields as\n"
         "    not_applicable\n"
         "  --warmup <uint> --reps <uint>\n"
         "  --format text|csv|json\n";
}

TimeBenchOptions ParseTimeBenchOptions(int argc, char** argv) {
  TimeBenchOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--help" || argument == "-h") {
      continue;
    }

    std::string key;
    std::string value;
    const std::size_t equals = argument.find('=');
    if (equals == std::string::npos) {
      key = argument;
      if (i + 1 >= argc) {
        throw std::invalid_argument("missing value after " + key);
      }
      value = argv[++i];
    } else {
      key = argument.substr(0, equals);
      value = argument.substr(equals + 1);
    }

    if (key == "--protocol") {
      options.protocols = swgr::bench::ParseProtocols(value);
    } else if (key == "--p") {
      options.p = swgr::bench::ParseUint64(key, value);
    } else if (key == "--k-exp") {
      options.k_exp = swgr::bench::ParseUint64(key, value);
    } else if (key == "--r") {
      options.r = swgr::bench::ParseUint64(key, value);
      options.r_was_set = true;
    } else if (key == "--n") {
      options.n = swgr::bench::ParseUint64(key, value);
    } else if (key == "--d") {
      options.d = swgr::bench::ParseUint64(key, value);
    } else if (key == "--fri-soundness-mode") {
      options.fri_soundness_mode = ParseFriSoundnessMode(value);
    } else if (key == "--fri-repetitions") {
      options.fri_repetitions = swgr::bench::ParseUint64(key, value);
    } else if (key == "--lambda") {
      options.lambda_target = swgr::bench::ParseUint64(key, value);
    } else if (key == "--pow-bits") {
      options.pow_bits = swgr::bench::ParseUint64(key, value);
    } else if (key == "--sec-mode") {
      options.sec_mode = swgr::bench::ParseSecurityMode(value);
    } else if (key == "--hash-profile") {
      options.hash_profile = swgr::bench::ParseHashProfile(value);
    } else if (key == "--stop-degree") {
      options.stop_degree = swgr::bench::ParseUint64(key, value);
    } else if (key == "--ood-samples") {
      options.ood_samples = swgr::bench::ParseUint64(key, value);
    } else if (key == "--queries") {
      if (swgr::bench::ToLowerCopy(value) == "theorem_auto") {
        options.stir_query_theorem_auto = true;
        options.queries.clear();
      } else {
        options.queries = swgr::bench::ParseQueries(value);
        options.stir_query_theorem_auto = false;
      }
    } else if (key == "--whir-m") {
      options.whir_m = swgr::bench::ParseUint64(key, value);
    } else if (key == "--whir-bmax") {
      options.whir_bmax = swgr::bench::ParseUint64(key, value);
    } else if (key == "--whir-rho0") {
      options.whir_rho0 = ParseWhirRational(key, value);
    } else if (key == "--whir-r" || key == "--whir-fixed-r") {
      options.whir_fixed_r = swgr::bench::ParseUint64(key, value);
    } else if (key == "--whir-polynomial") {
      options.whir_polynomial_kind = ParseWhirPolynomialKind(value);
    } else if (key == "--whir-repetitions") {
      options.whir_repetitions = swgr::bench::ParseUint64(key, value);
    } else if (key == "--threads") {
      options.threads = swgr::bench::ParseUint64(key, value);
    } else if (key == "--warmup") {
      options.warmup = swgr::bench::ParseUint64(key, value);
    } else if (key == "--reps") {
      options.reps = swgr::bench::ParseUint64(key, value);
    } else if (key == "--format") {
      options.format = swgr::bench::ParseOutputFormat(value);
    } else {
      throw std::invalid_argument("unknown option: " + key);
    }
  }

  if (options.n == 0 || options.d == 0 || options.d >= options.n) {
    throw std::invalid_argument("time bench requires 0 < d < n");
  }
  if (options.fri_repetitions.has_value() && *options.fri_repetitions == 0) {
    throw std::invalid_argument("--fri-repetitions must be > 0");
  }
  if (options.fri_soundness_mode == FriSoundnessMode::TheoremAuto &&
      options.lambda_target == 0) {
    throw std::invalid_argument(
        "--lambda must be > 0 when --fri-soundness-mode=theorem_auto");
  }
  if (options.threads == 0) {
    throw std::invalid_argument("--threads must be > 0");
  }
  if (options.reps == 0) {
    throw std::invalid_argument("--reps must be > 0");
  }
  if (options.whir_m == 0) {
    throw std::invalid_argument("--whir-m must be > 0");
  }
  if (options.whir_bmax == 0) {
    throw std::invalid_argument("--whir-bmax must be > 0");
  }
  if (options.r == 0) {
    throw std::invalid_argument("--r must be > 0");
  }
  if (options.whir_fixed_r.has_value() && *options.whir_fixed_r == 0) {
    throw std::invalid_argument("--whir-r/--whir-fixed-r must be > 0");
  }
  if (options.whir_repetitions.has_value() &&
      *options.whir_repetitions == 0) {
    throw std::invalid_argument("--whir-repetitions must be > 0");
  }
  return options;
}

swgr::poly_utils::Polynomial SamplePolynomial(
    const swgr::algebra::GRContext& ctx, const swgr::Domain& domain,
    std::size_t coefficient_count) {
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> coefficients;
    coefficients.reserve(coefficient_count);

    auto root_power = ctx.one();
    for (std::size_t i = 0; i < coefficient_count; ++i) {
      coefficients.push_back(root_power + ctx.one());
      root_power *= domain.root();
    }
    coefficients.back() += ctx.one();
    return swgr::poly_utils::Polynomial(std::move(coefficients));
  });
}

swgr::whir::MultiQuadraticPolynomial SampleWhirPolynomial(
    const swgr::algebra::GRContext& ctx, std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> coefficients;
    coefficients.reserve(
        static_cast<std::size_t>(swgr::whir::pow3_checked(variable_count)));
    auto current = ctx.one();
    const auto twist = ctx.teich_generator();
    for (std::uint64_t i = 0; i < swgr::whir::pow3_checked(variable_count);
         ++i) {
      coefficients.push_back(current + ctx.one());
      current *= twist;
    }
    coefficients.back() += ctx.one();
    return swgr::whir::MultiQuadraticPolynomial(variable_count,
                                                std::move(coefficients));
  });
}

swgr::whir::MultilinearPolynomial SampleWhirMultilinearPolynomial(
    const swgr::algebra::GRContext& ctx, std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> coefficients;
    coefficients.reserve(
        static_cast<std::size_t>(swgr::whir::pow2_checked(variable_count)));
    auto current = ctx.one();
    const auto twist = ctx.teich_generator();
    for (std::uint64_t i = 0; i < swgr::whir::pow2_checked(variable_count);
         ++i) {
      coefficients.push_back(current + ctx.one());
      current *= twist;
    }
    coefficients.back() += ctx.one();
    return swgr::whir::MultilinearPolynomial(variable_count,
                                             std::move(coefficients));
  });
}

std::vector<swgr::algebra::GRElem> SampleWhirOpenPoint(
    const swgr::algebra::GRContext& ctx, const swgr::Domain& domain,
    std::uint64_t variable_count) {
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> point;
    point.reserve(static_cast<std::size_t>(variable_count));
    for (std::uint64_t i = 0; i < variable_count; ++i) {
      point.push_back(ctx.one() + domain.element(i % domain.size()));
    }
    return point;
  });
}

void AddRunStats(TimeBenchRow& row, const swgr::ProofStatistics& prover_stats,
                 const swgr::ProofStatistics& verifier_stats) {
  row.commit_ms += prover_stats.commit_ms;
  row.prove_query_phase_ms += prover_stats.prove_query_phase_ms;
  row.prover_total_ms += prover_stats.prover_total_ms;
  row.verify_ms += verifier_stats.verifier_total_ms;
  row.serialized_bytes_actual = prover_stats.serialized_bytes;
  row.profile_prover_total_ms += prover_stats.prover_total_ms;
  row.profile_prover_encode_total_ms += prover_stats.prover_encode_ms;
  row.profile_prover_merkle_total_ms += prover_stats.prover_merkle_ms;
  row.profile_prover_transcript_total_ms += prover_stats.prover_transcript_ms;
  row.profile_prover_fold_total_ms += prover_stats.prover_fold_ms;
  row.profile_prover_interpolate_total_ms += prover_stats.prover_interpolate_ms;
  row.profile_prover_query_open_total_ms += prover_stats.prover_query_open_ms;
  row.profile_prover_ood_total_ms += prover_stats.prover_ood_ms;
  row.profile_prover_answer_total_ms += prover_stats.prover_answer_ms;
  row.profile_prover_quotient_total_ms += prover_stats.prover_quotient_ms;
  row.profile_prover_degree_correction_total_ms +=
      prover_stats.prover_degree_correction_ms;
  row.profile_verify_total_ms += verifier_stats.verifier_total_ms;
  row.profile_verify_merkle_total_ms += verifier_stats.verifier_merkle_ms;
  row.profile_verify_transcript_total_ms += verifier_stats.verifier_transcript_ms;
  row.profile_verify_query_total_ms += verifier_stats.verifier_query_phase_ms;
  row.profile_verify_algebra_total_ms += verifier_stats.verifier_algebra_ms;
}

void FinalizeMeans(TimeBenchRow& row) {
  row.commit_ms = SafeMean(row.commit_ms, row.reps);
  row.prove_query_phase_ms = SafeMean(row.prove_query_phase_ms, row.reps);
  row.prover_total_ms = SafeMean(row.prover_total_ms, row.reps);
  row.verify_ms = SafeMean(row.verify_ms, row.reps);
  row.serialized_kib_actual =
      static_cast<double>(row.serialized_bytes_actual) / 1024.0;
}

void PrintRowsText(const std::vector<TimeBenchRow>& rows) {
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    std::cout << "protocol=" << row.protocol << "\n";
    std::cout << "ring=" << row.ring << "\n";
    std::cout << "n=" << row.n << "\n";
    std::cout << "d=" << row.d << "\n";
    std::cout << "rho=" << row.rho << "\n";
    std::cout << "soundness_mode=" << row.soundness_mode << "\n";
    std::cout << "fri_repetitions=" << row.fri_repetitions << "\n";
    std::cout << "lambda_target=" << row.lambda_target << "\n";
    std::cout << "pow_bits=" << row.pow_bits << "\n";
    std::cout << "sec_mode=" << row.sec_mode << "\n";
    std::cout << "hash_profile=" << row.hash_profile << "\n";
    std::cout << "soundness_model=" << row.soundness_model << "\n";
    std::cout << "soundness_scope=" << row.soundness_scope << "\n";
    std::cout << "query_policy=" << row.query_policy << "\n";
    std::cout << "pow_policy=" << row.pow_policy << "\n";
    std::cout << "effective_security_bits=" << row.effective_security_bits
              << "\n";
    std::cout << "soundness_notes=" << row.soundness_notes << "\n";
    std::cout << "fold=" << row.fold << "\n";
    std::cout << "shift_power=" << row.shift_power << "\n";
    std::cout << "stop_degree=" << row.stop_degree << "\n";
    std::cout << "ood_samples=" << row.ood_samples << "\n";
    std::cout << "threads=" << row.threads << "\n";
    std::cout << "warmup=" << row.warmup << "\n";
    std::cout << "reps=" << row.reps << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "commit_ms=" << row.commit_ms << "\n"
              << "prove_query_phase_ms=" << row.prove_query_phase_ms << "\n"
              << "prover_total_ms=" << row.prover_total_ms << "\n"
              << "verify_ms=" << row.verify_ms << "\n"
              << "serialized_bytes_actual=" << row.serialized_bytes_actual
              << "\n"
              << "serialized_kib_actual=" << row.serialized_kib_actual << "\n"
              << "profile_prover_total_ms=" << row.profile_prover_total_ms
              << "\n"
              << "profile_prover_encode_total_ms="
              << row.profile_prover_encode_total_ms << "\n"
              << "profile_prover_merkle_total_ms="
              << row.profile_prover_merkle_total_ms << "\n"
              << "profile_prover_transcript_total_ms="
              << row.profile_prover_transcript_total_ms << "\n"
              << "profile_prover_fold_total_ms="
              << row.profile_prover_fold_total_ms << "\n"
              << "profile_prover_interpolate_total_ms="
              << row.profile_prover_interpolate_total_ms << "\n"
              << "profile_prover_query_open_total_ms="
              << row.profile_prover_query_open_total_ms << "\n"
              << "profile_prover_ood_total_ms="
              << row.profile_prover_ood_total_ms << "\n"
              << "profile_prover_answer_total_ms="
              << row.profile_prover_answer_total_ms << "\n"
              << "profile_prover_quotient_total_ms="
              << row.profile_prover_quotient_total_ms << "\n"
              << "profile_prover_degree_correction_total_ms="
              << row.profile_prover_degree_correction_total_ms << "\n"
              << "profile_prover_accounted_total_ms="
              << ProverProfileAccountedTotal(row) << "\n"
              << "profile_prover_unaccounted_total_ms="
              << ProverProfileUnaccountedTotal(row) << "\n"
              << "profile_prover_total_mean_ms="
              << SafeMean(row.profile_prover_total_ms, row.reps) << "\n"
              << "profile_prover_encode_mean_ms="
              << SafeMean(row.profile_prover_encode_total_ms, row.reps) << "\n"
              << "profile_prover_merkle_mean_ms="
              << SafeMean(row.profile_prover_merkle_total_ms, row.reps) << "\n"
              << "profile_prover_transcript_mean_ms="
              << SafeMean(row.profile_prover_transcript_total_ms, row.reps)
              << "\n"
              << "profile_prover_fold_mean_ms="
              << SafeMean(row.profile_prover_fold_total_ms, row.reps) << "\n"
              << "profile_prover_interpolate_mean_ms="
              << SafeMean(row.profile_prover_interpolate_total_ms, row.reps)
              << "\n"
              << "profile_prover_query_open_mean_ms="
              << SafeMean(row.profile_prover_query_open_total_ms, row.reps)
              << "\n"
              << "profile_prover_ood_mean_ms="
              << SafeMean(row.profile_prover_ood_total_ms, row.reps) << "\n"
              << "profile_prover_answer_mean_ms="
              << SafeMean(row.profile_prover_answer_total_ms, row.reps) << "\n"
              << "profile_prover_quotient_mean_ms="
              << SafeMean(row.profile_prover_quotient_total_ms, row.reps)
              << "\n"
              << "profile_prover_degree_correction_mean_ms="
              << SafeMean(row.profile_prover_degree_correction_total_ms, row.reps)
              << "\n"
              << "profile_prover_accounted_mean_ms="
              << SafeMean(ProverProfileAccountedTotal(row), row.reps) << "\n"
              << "profile_prover_unaccounted_mean_ms="
              << SafeMean(ProverProfileUnaccountedTotal(row), row.reps) << "\n"
              << "profile_verify_total_ms=" << row.profile_verify_total_ms
              << "\n"
              << "profile_verify_merkle_total_ms="
              << row.profile_verify_merkle_total_ms << "\n"
              << "profile_verify_transcript_total_ms="
              << row.profile_verify_transcript_total_ms << "\n"
              << "profile_verify_query_total_ms="
              << row.profile_verify_query_total_ms << "\n"
              << "profile_verify_algebra_total_ms="
              << row.profile_verify_algebra_total_ms << "\n"
              << "profile_verify_accounted_total_ms="
              << VerifyProfileAccountedTotal(row) << "\n"
              << "profile_verify_unaccounted_total_ms="
              << VerifyProfileUnaccountedTotal(row) << "\n"
              << "profile_verify_total_mean_ms="
              << SafeMean(row.profile_verify_total_ms, row.reps) << "\n"
              << "profile_verify_merkle_mean_ms="
              << SafeMean(row.profile_verify_merkle_total_ms, row.reps) << "\n"
              << "profile_verify_transcript_mean_ms="
              << SafeMean(row.profile_verify_transcript_total_ms, row.reps)
              << "\n"
              << "profile_verify_query_mean_ms="
              << SafeMean(row.profile_verify_query_total_ms, row.reps) << "\n"
              << "profile_verify_algebra_mean_ms="
              << SafeMean(row.profile_verify_algebra_total_ms, row.reps)
              << "\n"
              << "profile_verify_accounted_mean_ms="
              << SafeMean(VerifyProfileAccountedTotal(row), row.reps) << "\n"
              << "profile_verify_unaccounted_mean_ms="
              << SafeMean(VerifyProfileUnaccountedTotal(row), row.reps) << "\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "verifier_hashes_actual=" << row.verifier_hashes_actual << "\n";
    if (row_index + 1 != rows.size()) {
      std::cout << "\n";
    }
  }
}

void PrintRowsCsv(const std::vector<TimeBenchRow>& rows) {
  std::cout
      << "protocol,ring,n,d,rho,soundness_mode,fri_repetitions,lambda_target,"
         "pow_bits,sec_mode,hash_profile,soundness_model,soundness_scope,"
         "query_policy,pow_policy,effective_security_bits,"
         "soundness_notes,fold,shift_power,stop_degree,ood_samples,threads,"
         "warmup,reps,commit_ms,prove_query_phase_ms,prover_total_ms,"
         "verify_ms,serialized_bytes_actual,serialized_kib_actual,"
         "verifier_hashes_actual,profile_prover_total_ms,"
         "profile_prover_encode_total_ms,profile_prover_merkle_total_ms,"
         "profile_prover_transcript_total_ms,profile_prover_fold_total_ms,"
         "profile_prover_interpolate_total_ms,"
         "profile_prover_query_open_total_ms,profile_prover_ood_total_ms,"
         "profile_prover_answer_total_ms,profile_prover_quotient_total_ms,"
         "profile_prover_degree_correction_total_ms,"
         "profile_prover_accounted_total_ms,"
         "profile_prover_unaccounted_total_ms,profile_prover_total_mean_ms,"
         "profile_prover_encode_mean_ms,profile_prover_merkle_mean_ms,"
         "profile_prover_transcript_mean_ms,profile_prover_fold_mean_ms,"
         "profile_prover_interpolate_mean_ms,"
         "profile_prover_query_open_mean_ms,profile_prover_ood_mean_ms,"
         "profile_prover_answer_mean_ms,profile_prover_quotient_mean_ms,"
         "profile_prover_degree_correction_mean_ms,"
         "profile_prover_accounted_mean_ms,"
         "profile_prover_unaccounted_mean_ms,profile_verify_total_ms,"
         "profile_verify_merkle_total_ms,profile_verify_transcript_total_ms,"
         "profile_verify_query_total_ms,profile_verify_algebra_total_ms,"
         "profile_verify_accounted_total_ms,"
         "profile_verify_unaccounted_total_ms,profile_verify_total_mean_ms,"
         "profile_verify_merkle_mean_ms,"
         "profile_verify_transcript_mean_ms,profile_verify_query_mean_ms,"
         "profile_verify_algebra_mean_ms,profile_verify_accounted_mean_ms,"
         "profile_verify_unaccounted_mean_ms\n";
  for (const auto& row : rows) {
    std::cout << swgr::bench::CsvEscape(row.protocol) << ","
              << swgr::bench::CsvEscape(row.ring) << "," << row.n << ","
              << row.d << "," << swgr::bench::CsvEscape(row.rho) << ","
              << swgr::bench::CsvEscape(row.soundness_mode) << ","
              << row.fri_repetitions << "," << row.lambda_target << ","
              << row.pow_bits << ","
              << swgr::bench::CsvEscape(row.sec_mode) << ","
              << swgr::bench::CsvEscape(row.hash_profile) << ","
              << swgr::bench::CsvEscape(row.soundness_model) << ","
              << swgr::bench::CsvEscape(row.soundness_scope) << ","
              << swgr::bench::CsvEscape(row.query_policy) << ","
              << swgr::bench::CsvEscape(row.pow_policy) << ","
              << row.effective_security_bits << ","
              << swgr::bench::CsvEscape(row.soundness_notes) << ","
              << row.fold << "," << row.shift_power << "," << row.stop_degree
              << ","
              << row.ood_samples << "," << row.threads << "," << row.warmup
              << "," << row.reps << "," << std::fixed << std::setprecision(3)
              << row.commit_ms << "," << row.prove_query_phase_ms << ","
              << row.prover_total_ms << "," << row.verify_ms << ","
              << row.serialized_bytes_actual << ","
              << row.serialized_kib_actual << ","
              << row.verifier_hashes_actual << ","
              << row.profile_prover_total_ms << ","
              << row.profile_prover_encode_total_ms << ","
              << row.profile_prover_merkle_total_ms << ","
              << row.profile_prover_transcript_total_ms << ","
              << row.profile_prover_fold_total_ms << ","
              << row.profile_prover_interpolate_total_ms << ","
              << row.profile_prover_query_open_total_ms << ","
              << row.profile_prover_ood_total_ms << ","
              << row.profile_prover_answer_total_ms << ","
              << row.profile_prover_quotient_total_ms << ","
              << row.profile_prover_degree_correction_total_ms << ","
              << ProverProfileAccountedTotal(row) << ","
              << ProverProfileUnaccountedTotal(row) << ","
              << SafeMean(row.profile_prover_total_ms, row.reps) << ","
              << SafeMean(row.profile_prover_encode_total_ms, row.reps) << ","
              << SafeMean(row.profile_prover_merkle_total_ms, row.reps) << ","
              << SafeMean(row.profile_prover_transcript_total_ms, row.reps)
              << "," << SafeMean(row.profile_prover_fold_total_ms, row.reps)
              << ","
              << SafeMean(row.profile_prover_interpolate_total_ms, row.reps)
              << ","
              << SafeMean(row.profile_prover_query_open_total_ms, row.reps)
              << "," << SafeMean(row.profile_prover_ood_total_ms, row.reps)
              << "," << SafeMean(row.profile_prover_answer_total_ms, row.reps)
              << ","
              << SafeMean(row.profile_prover_quotient_total_ms, row.reps)
              << ","
              << SafeMean(row.profile_prover_degree_correction_total_ms, row.reps)
              << "," << SafeMean(ProverProfileAccountedTotal(row), row.reps)
              << "," << SafeMean(ProverProfileUnaccountedTotal(row), row.reps)
              << ","
              << row.profile_verify_total_ms << ","
              << row.profile_verify_merkle_total_ms << ","
              << row.profile_verify_transcript_total_ms << ","
              << row.profile_verify_query_total_ms << ","
              << row.profile_verify_algebra_total_ms << ","
              << VerifyProfileAccountedTotal(row) << ","
              << VerifyProfileUnaccountedTotal(row) << ","
              << SafeMean(row.profile_verify_total_ms, row.reps) << ","
              << SafeMean(row.profile_verify_merkle_total_ms, row.reps) << ","
              << SafeMean(row.profile_verify_transcript_total_ms, row.reps)
              << "," << SafeMean(row.profile_verify_query_total_ms, row.reps)
              << "," << SafeMean(row.profile_verify_algebra_total_ms, row.reps)
              << "," << SafeMean(VerifyProfileAccountedTotal(row), row.reps)
              << "," << SafeMean(VerifyProfileUnaccountedTotal(row), row.reps)
              << "\n";
    std::cout.unsetf(std::ios::floatfield);
  }
}

void PrintRowsJson(const std::vector<TimeBenchRow>& rows) {
  std::cout << "[\n";
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    std::cout << "  {\n";
    std::cout << "    \"protocol\": \""
              << swgr::bench::JsonEscape(row.protocol) << "\",\n";
    std::cout << "    \"ring\": \"" << swgr::bench::JsonEscape(row.ring)
              << "\",\n";
    std::cout << "    \"n\": " << row.n << ",\n";
    std::cout << "    \"d\": " << row.d << ",\n";
    std::cout << "    \"rho\": \"" << swgr::bench::JsonEscape(row.rho)
              << "\",\n";
    std::cout << "    \"soundness_mode\": \""
              << swgr::bench::JsonEscape(row.soundness_mode) << "\",\n";
    std::cout << "    \"fri_repetitions\": " << row.fri_repetitions << ",\n";
    std::cout << "    \"lambda_target\": " << row.lambda_target << ",\n";
    std::cout << "    \"pow_bits\": " << row.pow_bits << ",\n";
    std::cout << "    \"sec_mode\": \"" << swgr::bench::JsonEscape(row.sec_mode)
              << "\",\n";
    std::cout << "    \"hash_profile\": \""
              << swgr::bench::JsonEscape(row.hash_profile) << "\",\n";
    std::cout << "    \"soundness_model\": \""
              << swgr::bench::JsonEscape(row.soundness_model) << "\",\n";
    std::cout << "    \"soundness_scope\": \""
              << swgr::bench::JsonEscape(row.soundness_scope) << "\",\n";
    std::cout << "    \"query_policy\": \""
              << swgr::bench::JsonEscape(row.query_policy) << "\",\n";
    std::cout << "    \"pow_policy\": \""
              << swgr::bench::JsonEscape(row.pow_policy) << "\",\n";
    std::cout << "    \"effective_security_bits\": "
              << row.effective_security_bits << ",\n";
    std::cout << "    \"soundness_notes\": \""
              << swgr::bench::JsonEscape(row.soundness_notes) << "\",\n";
    std::cout << "    \"fold\": " << row.fold << ",\n";
    std::cout << "    \"shift_power\": " << row.shift_power << ",\n";
    std::cout << "    \"stop_degree\": " << row.stop_degree << ",\n";
    std::cout << "    \"ood_samples\": " << row.ood_samples << ",\n";
    std::cout << "    \"threads\": " << row.threads << ",\n";
    std::cout << "    \"warmup\": " << row.warmup << ",\n";
    std::cout << "    \"reps\": " << row.reps << ",\n";
    std::cout << std::fixed << std::setprecision(3)
              << "    \"commit_ms\": " << row.commit_ms << ",\n"
              << "    \"prove_query_phase_ms\": " << row.prove_query_phase_ms
              << ",\n"
              << "    \"prover_total_ms\": " << row.prover_total_ms << ",\n"
              << "    \"verify_ms\": " << row.verify_ms << ",\n"
              << "    \"serialized_bytes_actual\": "
              << row.serialized_bytes_actual << ",\n"
              << "    \"serialized_kib_actual\": "
              << row.serialized_kib_actual << ",\n"
              << "    \"profile_prover_total_ms\": "
              << row.profile_prover_total_ms << ",\n"
              << "    \"profile_prover_encode_total_ms\": "
              << row.profile_prover_encode_total_ms << ",\n"
              << "    \"profile_prover_merkle_total_ms\": "
              << row.profile_prover_merkle_total_ms << ",\n"
              << "    \"profile_prover_transcript_total_ms\": "
              << row.profile_prover_transcript_total_ms << ",\n"
              << "    \"profile_prover_fold_total_ms\": "
              << row.profile_prover_fold_total_ms << ",\n"
              << "    \"profile_prover_interpolate_total_ms\": "
              << row.profile_prover_interpolate_total_ms << ",\n"
              << "    \"profile_prover_query_open_total_ms\": "
              << row.profile_prover_query_open_total_ms << ",\n"
              << "    \"profile_prover_ood_total_ms\": "
              << row.profile_prover_ood_total_ms << ",\n"
              << "    \"profile_prover_answer_total_ms\": "
              << row.profile_prover_answer_total_ms << ",\n"
              << "    \"profile_prover_quotient_total_ms\": "
              << row.profile_prover_quotient_total_ms << ",\n"
              << "    \"profile_prover_degree_correction_total_ms\": "
              << row.profile_prover_degree_correction_total_ms << ",\n"
              << "    \"profile_prover_accounted_total_ms\": "
              << ProverProfileAccountedTotal(row) << ",\n"
              << "    \"profile_prover_unaccounted_total_ms\": "
              << ProverProfileUnaccountedTotal(row) << ",\n"
              << "    \"profile_prover_total_mean_ms\": "
              << SafeMean(row.profile_prover_total_ms, row.reps) << ",\n"
              << "    \"profile_prover_encode_mean_ms\": "
              << SafeMean(row.profile_prover_encode_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_merkle_mean_ms\": "
              << SafeMean(row.profile_prover_merkle_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_transcript_mean_ms\": "
              << SafeMean(row.profile_prover_transcript_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_fold_mean_ms\": "
              << SafeMean(row.profile_prover_fold_total_ms, row.reps) << ",\n"
              << "    \"profile_prover_interpolate_mean_ms\": "
              << SafeMean(row.profile_prover_interpolate_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_query_open_mean_ms\": "
              << SafeMean(row.profile_prover_query_open_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_ood_mean_ms\": "
              << SafeMean(row.profile_prover_ood_total_ms, row.reps) << ",\n"
              << "    \"profile_prover_answer_mean_ms\": "
              << SafeMean(row.profile_prover_answer_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_quotient_mean_ms\": "
              << SafeMean(row.profile_prover_quotient_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_degree_correction_mean_ms\": "
              << SafeMean(row.profile_prover_degree_correction_total_ms, row.reps)
              << ",\n"
              << "    \"profile_prover_accounted_mean_ms\": "
              << SafeMean(ProverProfileAccountedTotal(row), row.reps) << ",\n"
              << "    \"profile_prover_unaccounted_mean_ms\": "
              << SafeMean(ProverProfileUnaccountedTotal(row), row.reps) << ",\n"
              << "    \"profile_verify_total_ms\": "
              << row.profile_verify_total_ms << ",\n"
              << "    \"profile_verify_merkle_total_ms\": "
              << row.profile_verify_merkle_total_ms << ",\n"
              << "    \"profile_verify_transcript_total_ms\": "
              << row.profile_verify_transcript_total_ms << ",\n"
              << "    \"profile_verify_query_total_ms\": "
              << row.profile_verify_query_total_ms << ",\n"
              << "    \"profile_verify_algebra_total_ms\": "
              << row.profile_verify_algebra_total_ms << ",\n"
              << "    \"profile_verify_accounted_total_ms\": "
              << VerifyProfileAccountedTotal(row) << ",\n"
              << "    \"profile_verify_unaccounted_total_ms\": "
              << VerifyProfileUnaccountedTotal(row) << ",\n"
              << "    \"profile_verify_total_mean_ms\": "
              << SafeMean(row.profile_verify_total_ms, row.reps) << ",\n"
              << "    \"profile_verify_merkle_mean_ms\": "
              << SafeMean(row.profile_verify_merkle_total_ms, row.reps)
              << ",\n"
              << "    \"profile_verify_transcript_mean_ms\": "
              << SafeMean(row.profile_verify_transcript_total_ms, row.reps)
              << ",\n"
              << "    \"profile_verify_query_mean_ms\": "
              << SafeMean(row.profile_verify_query_total_ms, row.reps) << ",\n"
              << "    \"profile_verify_algebra_mean_ms\": "
              << SafeMean(row.profile_verify_algebra_total_ms, row.reps)
              << ",\n"
              << "    \"profile_verify_accounted_mean_ms\": "
              << SafeMean(VerifyProfileAccountedTotal(row), row.reps) << ",\n"
              << "    \"profile_verify_unaccounted_mean_ms\": "
              << SafeMean(VerifyProfileUnaccountedTotal(row), row.reps)
              << ",\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "    \"verifier_hashes_actual\": "
              << row.verifier_hashes_actual << "\n";
    std::cout << "  }";
    if (row_index + 1 != rows.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "]\n";
}

void PrintRows(const std::vector<TimeBenchRow>& rows,
               swgr::bench::OutputFormat format) {
  switch (format) {
    case swgr::bench::OutputFormat::Text:
      PrintRowsText(rows);
      return;
    case swgr::bench::OutputFormat::Csv:
      PrintRowsCsv(rows);
      return;
    case swgr::bench::OutputFormat::Json:
      PrintRowsJson(rows);
      return;
  }
}

void PrintQueryWarnings(
    std::string_view protocol,
    const std::vector<swgr::stir::RoundQueryScheduleMetadata>& metadata) {
  for (std::size_t round_index = 0; round_index < metadata.size(); ++round_index) {
    const auto& round = metadata[round_index];
    if (!round.cap_applied) {
      continue;
    }
    std::cerr << "warning: " << protocol << " round " << round_index
              << " requested " << round.requested_query_count
              << " queries, capped to " << round.effective_query_count
              << " (bundle_count=" << round.bundle_count
              << ", degree_budget=" << round.degree_budget << ")\n";
  }
}

template <typename RunMeasured>
TimeBenchRow RunBenchLoop(const TimeBenchOptions& options, std::string protocol,
                          std::uint64_t fold, std::uint64_t shift_power,
                          std::uint64_t ood_samples, RunMeasured&& run_measured) {
  TimeBenchRow row;
  row.protocol = std::move(protocol);
  row.ring = swgr::bench::RingString(options.p, options.k_exp, options.r);
  row.n = options.n;
  row.d = options.d;
  row.rho = swgr::bench::ReducedRatioString(options.d, options.n);
  row.hash_profile = swgr::to_string(options.hash_profile);
  row.fold = fold;
  row.shift_power = shift_power;
  row.stop_degree = options.stop_degree;
  row.ood_samples = ood_samples;
  row.threads = options.threads;
  row.warmup = options.warmup;
  row.reps = options.reps;

  for (std::uint64_t warmup_index = 0; warmup_index < options.warmup; ++warmup_index) {
    run_measured(nullptr);
  }
  for (std::uint64_t rep = 0; rep < options.reps; ++rep) {
    run_measured(&row);
  }
  FinalizeMeans(row);
  return row;
}

TimeBenchRow MakeFriRow(const TimeBenchOptions& options,
                        const std::shared_ptr<const swgr::algebra::GRContext>& ctx,
                        std::uint64_t fold_factor) {
  const auto resolved_soundness = ResolveFriSoundness(options, fold_factor);

  swgr::fri::FriParameters params;
  params.fold_factor = fold_factor;
  params.stop_degree = options.stop_degree;
  params.repetition_count = resolved_soundness.repetition_count;
  params.hash_profile = options.hash_profile;

  const swgr::fri::FriInstance instance{
      .domain = swgr::Domain::teichmuller_subgroup(ctx, options.n),
      .claimed_degree = options.d,
  };
  const auto polynomial =
      SamplePolynomial(*ctx, instance.domain, static_cast<std::size_t>(options.d + 1));
  const swgr::fri::FriProver prover(params);
  const swgr::fri::FriVerifier verifier(params);

  auto row = RunBenchLoop(options, fold_factor == 3 ? "fri3" : "fri9", fold_factor,
                          0, 0, [&](TimeBenchRow* current_row) {
                        const auto prover_start = std::chrono::steady_clock::now();
                        const auto commitment = prover.commit(instance, polynomial);
                        const double commit_ms = ElapsedMilliseconds(
                            prover_start, std::chrono::steady_clock::now());
                        const auto opening =
                            prover.open(commitment, polynomial, ctx->zero());
                        const double prover_total_ms = ElapsedMilliseconds(
                            prover_start, std::chrono::steady_clock::now());
                        swgr::ProofStatistics verify_stats;
                        if (!verifier.verify(commitment, opening.claim.alpha,
                                             opening.claim.value, opening,
                                             &verify_stats)) {
                          throw std::runtime_error("fri verifier rejected honest proof");
                        }
                        if (current_row != nullptr) {
                          auto prover_stats = opening.proof.stats;
                          prover_stats.commit_ms += commit_ms;
                          prover_stats.prover_total_ms = prover_total_ms;
                          AddRunStats(*current_row, prover_stats, verify_stats);
                          current_row->verifier_hashes_actual =
                              opening.proof.stats.verifier_hashes;
                        }
                      });
  FillFriSoundnessMetadata(row, resolved_soundness);
  return row;
}

TimeBenchRow MakeStirRow(const TimeBenchOptions& options,
                         const std::shared_ptr<const swgr::algebra::GRContext>& ctx) {
  swgr::stir::StirParameters params;
  params.virtual_fold_factor = 9;
  params.shift_power = 3;
  params.ood_samples = options.ood_samples;
  params.query_repetitions = options.queries;
  params.stop_degree = options.stop_degree;
  params.lambda_target = options.lambda_target;
  params.pow_bits = options.pow_bits;
  params.sec_mode = options.sec_mode;
  params.hash_profile = options.hash_profile;
  params.protocol_mode = swgr::stir::StirProtocolMode::TheoremGr;
  params.challenge_sampling = swgr::stir::StirChallengeSampling::TeichmullerT;
  params.ood_sampling =
      swgr::stir::StirOodSamplingMode::TheoremExceptionalComplementUnique;

  const swgr::stir::StirInstance instance{
      .domain = swgr::Domain::teichmuller_subgroup(ctx, options.n),
      .claimed_degree = options.d,
  };
  std::string query_policy = "auto_live_schedule";
  if (options.stir_query_theorem_auto) {
    const auto solved =
        swgr::stir::solve_min_query_schedule_for_lambda(params, instance);
    params.query_repetitions = solved.query_schedule;
    query_policy = "theorem_auto_solved_schedule";
  } else if (!options.queries.empty()) {
    query_policy = "manual_live_schedule";
  }
  PrintQueryWarnings("stir9to3",
                     swgr::stir::resolve_query_schedule_metadata(params, instance));
  const auto polynomial =
      SamplePolynomial(*ctx, instance.domain, static_cast<std::size_t>(options.d + 1));
  const swgr::stir::StirProver prover(params);
  const swgr::stir::StirVerifier verifier(params);

  auto row = RunBenchLoop(options, "stir9to3", 9, 3, options.ood_samples,
                          [&](TimeBenchRow* row) {
                            const auto proof = prover.prove(instance, polynomial);
                            swgr::ProofStatistics verify_stats;
                            if (!verifier.verify(instance, proof, &verify_stats)) {
                              throw std::runtime_error(
                                  "stir verifier rejected honest proof");
                            }
                            if (row != nullptr) {
                              AddRunStats(*row, proof.stats, verify_stats);
                              row->verifier_hashes_actual =
                                  proof.stats.verifier_hashes;
                            }
                          });
  const auto analysis = swgr::stir::analyze_theorem_soundness(params, instance);
  FillStirTheoremSoundnessMetadata(row, analysis, options.sec_mode,
                                   options.lambda_target, options.pow_bits,
                                   query_policy);
  return row;
}

TimeBenchRow MakeWhirRow(const TimeBenchOptions& options) {
  if (options.p != 2) {
    throw std::invalid_argument("whir_gr_ud currently requires --p 2");
  }

  const std::optional<std::uint64_t> fixed_whir_r =
      options.whir_fixed_r.has_value()
          ? options.whir_fixed_r
          : (options.r_was_set ? std::optional<std::uint64_t>(options.r)
                               : std::nullopt);
  if (fixed_whir_r.has_value() && *fixed_whir_r == 0) {
    throw std::invalid_argument("WHIR fixed r must be non-zero");
  }

  auto selection = swgr::whir::select_whir_unique_decoding_parameters(
      swgr::whir::WhirUniqueDecodingInputs{
          .lambda_target = options.lambda_target,
          .ring_exponent = options.k_exp,
          .variable_count = options.whir_m,
          .max_layer_width = options.whir_bmax,
          .rho0 = options.whir_rho0,
          .fixed_extension_degree = fixed_whir_r.value_or(0),
      });
  if (!selection.feasible) {
    throw std::invalid_argument("WHIR unique-decoding selector found no "
                                "feasible parameters: " +
                                JoinNotes(selection.notes));
  }

  auto ctx = std::make_shared<swgr::algebra::GRContext>(swgr::algebra::GRConfig{
      .p = 2,
      .k_exp = options.k_exp,
      .r = selection.selected_r,
  });
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(
      ctx, selection.public_params.initial_domain_size);
  const auto pp = ctx->with_ntl_context([&] {
    const auto omega = NTL::power(
        domain.root(),
        static_cast<long>(selection.public_params.initial_domain_size / 3U));
    auto shift_repetitions = selection.public_params.shift_repetitions;
    auto final_repetitions = selection.public_params.final_repetitions;
    if (options.whir_repetitions.has_value()) {
      std::fill(shift_repetitions.begin(), shift_repetitions.end(),
                *options.whir_repetitions);
      final_repetitions = *options.whir_repetitions;
    }
    return swgr::whir::WhirPublicParameters{
        .ctx = ctx,
        .initial_domain = domain,
        .variable_count = options.whir_m,
        .layer_widths = selection.public_params.layer_widths,
        .shift_repetitions = std::move(shift_repetitions),
        .final_repetitions = final_repetitions,
        .degree_bounds = selection.public_params.degree_bounds,
        .deltas = selection.public_params.deltas,
        .omega = omega,
        .ternary_grid = {ctx->one(), omega, omega * omega},
        .lambda_target = options.lambda_target,
        .hash_profile = options.hash_profile,
    };
  });

  swgr::whir::WhirParameters params;
  params.lambda_target = options.lambda_target;
  params.hash_profile = options.hash_profile;
  const auto multiquadratic_polynomial =
      options.whir_polynomial_kind == WhirPolynomialKind::MultiQuadratic
          ? std::optional<swgr::whir::MultiQuadraticPolynomial>(
                SampleWhirPolynomial(*ctx, options.whir_m))
          : std::nullopt;
  const auto multilinear_polynomial =
      options.whir_polynomial_kind == WhirPolynomialKind::Multilinear
          ? std::optional<swgr::whir::MultilinearPolynomial>(
                SampleWhirMultilinearPolynomial(*ctx, options.whir_m))
          : std::nullopt;
  const auto point = SampleWhirOpenPoint(*ctx, pp.initial_domain, options.whir_m);
  const swgr::whir::WhirProver prover(params);
  const swgr::whir::WhirVerifier verifier(params);

  auto row = RunBenchLoop(options, "whir_gr_ud", 3, 3, 0,
                          [&](TimeBenchRow* current_row) {
                            swgr::whir::WhirCommitmentState state;
                            const auto commitment =
                                multilinear_polynomial.has_value()
                                    ? prover.commit(pp, *multilinear_polynomial,
                                                    &state)
                                    : prover.commit(pp,
                                                    *multiquadratic_polynomial,
                                                    &state);
                            const auto opening =
                                prover.open(commitment, state, point);
                            swgr::ProofStatistics verify_stats;
                            if (!verifier.verify(commitment, point, opening,
                                                 &verify_stats)) {
                              throw std::runtime_error(
                                  "WHIR verifier rejected honest proof");
                            }
                            if (current_row != nullptr) {
                              auto prover_stats = opening.proof.stats;
                              prover_stats.commit_ms =
                                  commitment.stats.commit_ms;
                              prover_stats.prover_encode_ms +=
                                  commitment.stats.prover_encode_ms;
                              prover_stats.prover_merkle_ms +=
                                  commitment.stats.prover_merkle_ms;
                              prover_stats.prover_total_ms +=
                                  commitment.stats.commit_ms;
                              AddRunStats(*current_row, prover_stats,
                                          verify_stats);
                            }
                          });

  const std::uint64_t code_dimension = swgr::whir::pow3_checked(options.whir_m);
  row.ring = swgr::bench::RingString(2, options.k_exp, selection.selected_r);
  row.n = pp.initial_domain.size();
  row.d = code_dimension;
  row.rho = swgr::bench::ReducedRatioString(code_dimension, row.n);
  row.soundness_mode = "theorem_whir_gr_unique_decoding";
  row.fri_repetitions = 0;
  row.lambda_target = options.lambda_target;
  row.pow_bits = 0;
  row.sec_mode.clear();
  row.soundness_model = "epsilon_iopp_whir_gr_unique_decoding";
  row.soundness_scope = "whir_gr2k_pcs_unique_decoding";
  row.query_policy = options.whir_repetitions.has_value()
                         ? "debug_manual_whir_repetitions"
                         : "selector_shift_and_final_repetitions";
  row.pow_policy = "not_applicable";
  row.effective_security_bits = selection.effective_security_bits;
  auto notes = selection.notes;
  notes.push_back("selected_r=" + std::to_string(selection.selected_r));
  if (fixed_whir_r.has_value()) {
    notes.push_back("fixed_r=" + std::to_string(*fixed_whir_r));
  }
  notes.push_back("initial_domain_size=" + std::to_string(row.n));
  if (options.whir_polynomial_kind == WhirPolynomialKind::Multilinear) {
    notes.push_back("polynomial_family=multilinear_embedded_in_ternary_"
                    "multi_quadratic_code");
    notes.push_back("multilinear_coefficient_count=" +
                    std::to_string(swgr::whir::pow2_checked(options.whir_m)));
    notes.push_back("soundness_dimension_remains_3^m=" +
                    std::to_string(code_dimension));
  } else {
    notes.push_back("polynomial_family=multi_quadratic");
  }
  if (options.whir_repetitions.has_value()) {
    notes.push_back("manual --whir-repetitions overrides selector repetitions "
                    "for debugging only");
  }
  row.soundness_notes = JoinNotes(notes);
  return row;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (swgr::bench::WantsHelp(argc, argv)) {
      std::cout << TimeBenchUsage(argv[0]);
      return 0;
    }

    const auto options = ParseTimeBenchOptions(argc, argv);
    ApplyThreadControl(options.threads);
    auto ctx = std::make_shared<swgr::algebra::GRContext>(swgr::algebra::GRConfig{
        .p = options.p,
        .k_exp = options.k_exp,
        .r = options.r,
    });
    (void)ctx->teich_generator();

    std::vector<TimeBenchRow> rows;
    rows.reserve(options.protocols.size());
    for (const auto& protocol : options.protocols) {
      if (protocol == "fri3") {
        rows.push_back(MakeFriRow(options, ctx, 3));
      } else if (protocol == "fri9") {
        rows.push_back(MakeFriRow(options, ctx, 9));
      } else if (protocol == "stir9to3") {
        rows.push_back(MakeStirRow(options, ctx));
      } else if (protocol == "whir_gr_ud") {
        rows.push_back(MakeWhirRow(options));
      } else {
        throw std::invalid_argument("unsupported protocol in dispatch: " +
                                    protocol);
      }
    }

    PrintRows(rows, options.format);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "bench_time failed: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "bench_time failed: unknown exception\n";
    return 1;
  }
}
