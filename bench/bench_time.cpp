#include "bench_common.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(SWGR_HAS_OPENMP)
#include <omp.h>
#endif

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "fri/prover.hpp"
#include "fri/verifier.hpp"
#include "ldt.hpp"
#include "poly_utils/polynomial.hpp"
#include "soundness/configurator.hpp"
#include "stir/prover.hpp"
#include "stir/verifier.hpp"

namespace {

struct TimeBenchOptions {
  std::vector<std::string> protocols = {"fri3", "fri9", "stir9to3"};
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 162;
  std::uint64_t n = 243;
  std::uint64_t d = 81;
  std::uint64_t fri_repetitions = 1;
  std::uint64_t lambda_target = 128;
  std::uint64_t pow_bits = 0;
  swgr::SecurityMode sec_mode = swgr::SecurityMode::ConjectureCapacity;
  swgr::HashProfile hash_profile = swgr::HashProfile::STIR_NATIVE;
  std::uint64_t stop_degree = 9;
  std::uint64_t ood_samples = 2;
  std::vector<std::uint64_t> queries;
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
                              std::uint64_t repetition_count) {
  row.soundness_mode = "theorem_fri";
  row.fri_repetitions = repetition_count;
  row.lambda_target = 0;
  row.pow_bits = 0;
  row.sec_mode.clear();
  row.soundness_model = "paper_repetition_count_m";
  row.soundness_scope = "paper_parameterization_m_only";
  row.query_policy = "repeat_steps_3_4_5";
  row.pow_policy = "not_applicable";
  row.effective_security_bits = 0;
  row.soundness_notes =
      "FRI rows report the theorem-facing repetition count m directly; "
      "benchmark output does not derive security bits from m here.";
}

void FillEngineeringSoundnessMetadata(
    TimeBenchRow& row, swgr::SecurityMode sec_mode,
    std::uint64_t lambda_target, std::uint64_t pow_bits,
    const std::vector<std::uint64_t>& query_repetitions, double rho) {
  const auto heuristic = swgr::soundness::engineering_heuristic_result(
      sec_mode, lambda_target, pow_bits, !query_repetitions.empty(), rho);
  row.soundness_mode = "engineering_stir";
  row.fri_repetitions = 0;
  row.lambda_target = lambda_target;
  row.pow_bits = pow_bits;
  row.sec_mode = swgr::to_string(sec_mode);
  row.soundness_model = heuristic.model;
  row.soundness_scope = heuristic.scope;
  row.query_policy = heuristic.query_policy;
  row.pow_policy = heuristic.pow_policy;
  row.effective_security_bits = heuristic.effective_security_bits;
  row.soundness_notes = JoinNotes(heuristic.notes);
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
         "  --protocol fri3|fri9|stir9to3|all\n"
         "  --p <uint> --k-exp <uint> --r <uint>\n"
         "  --n <uint> --d <uint>\n"
         "  --fri-repetitions <uint>\n"
         "  --lambda <uint> --pow-bits <uint>\n"
         "  --sec-mode ConjectureCapacity|Conservative\n"
         "  --hash-profile STIR_NATIVE|WHIR_NATIVE\n"
         "  --stop-degree <uint> --ood-samples <uint>\n"
         "  --queries auto|q0[,q1,...] --threads <uint>\n"
         "    note: fri3/fri9 use --fri-repetitions as the theorem-facing FRI\n"
         "    repetition parameter m\n"
         "    note: --lambda/--pow-bits/--sec-mode/--queries remain the current\n"
         "    STIR engineering / benchmark knobs\n"
         "    note: text/csv/json output currently keeps one shared row schema\n"
         "    across FRI and STIR; on theorem_fri rows the engineering fields\n"
         "    lambda_target/pow_bits/sec_mode/effective_security_bits are\n"
         "    placeholders and should be read as not_applicable\n"
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
    } else if (key == "--n") {
      options.n = swgr::bench::ParseUint64(key, value);
    } else if (key == "--d") {
      options.d = swgr::bench::ParseUint64(key, value);
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
      options.queries = swgr::bench::ParseQueries(value);
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

  if (options.n == 0 || options.d >= options.n) {
    throw std::invalid_argument("time bench requires 0 < d < n");
  }
  if (options.fri_repetitions == 0) {
    throw std::invalid_argument("--fri-repetitions must be > 0");
  }
  if (options.threads == 0) {
    throw std::invalid_argument("--threads must be > 0");
  }
  if (options.reps == 0) {
    throw std::invalid_argument("--reps must be > 0");
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
  swgr::fri::FriParameters params;
  params.fold_factor = fold_factor;
  params.stop_degree = options.stop_degree;
  params.repetition_count = options.fri_repetitions;
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
  FillFriSoundnessMetadata(row, options.fri_repetitions);
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

  const swgr::stir::StirInstance instance{
      .domain = swgr::Domain::teichmuller_subgroup(ctx, options.n),
      .claimed_degree = options.d,
  };
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
  FillEngineeringSoundnessMetadata(
      row, options.sec_mode, options.lambda_target, options.pow_bits,
      options.queries,
      static_cast<double>(options.d + 1U) / static_cast<double>(options.n));
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
