#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "algebra/gr_context.hpp"
#include "bench_common.hpp"
#include "domain.hpp"
#include "stir/soundness.hpp"

namespace {

struct Options {
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 162;
  std::uint64_t n = 243;
  std::uint64_t d = 81;
  std::uint64_t lambda_target = 128;
  std::uint64_t pow_bits = 0;
  stir_whir_gr::SecurityMode sec_mode = stir_whir_gr::SecurityMode::ConjectureCapacity;
  stir_whir_gr::HashProfile hash_profile = stir_whir_gr::HashProfile::STIR_NATIVE;
  std::uint64_t stop_degree = 9;
  std::uint64_t ood_samples = 2;
  stir_whir_gr::bench::OutputFormat format = stir_whir_gr::bench::OutputFormat::Text;
};

std::string Usage(const char* argv0) {
  return std::string("Usage: ") + argv0 +
         " --n <uint> --d <uint> [options]\n"
         "  --p <uint> --k-exp <uint> --r <uint>\n"
         "  --lambda <uint> --pow-bits <uint>\n"
         "  --sec-mode ConjectureCapacity|Conservative\n"
         "  --hash-profile STIR_NATIVE|WHIR_NATIVE\n"
         "  --stop-degree <uint> --ood-samples <uint>\n"
         "  --format text|json\n";
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string key(argv[i]);
    if (key == "--help" || key == "-h") {
      std::cout << Usage(argv[0]);
      std::exit(0);
    }
    if (i + 1 >= argc) {
      throw std::invalid_argument("missing value for " + key);
    }
    const std::string value(argv[++i]);
    if (key == "--p") {
      options.p = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--k-exp") {
      options.k_exp = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--r") {
      options.r = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--n") {
      options.n = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--d") {
      options.d = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--lambda") {
      options.lambda_target = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--pow-bits") {
      options.pow_bits = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--sec-mode") {
      options.sec_mode = stir_whir_gr::bench::ParseSecurityMode(value);
    } else if (key == "--hash-profile") {
      options.hash_profile = stir_whir_gr::bench::ParseHashProfile(value);
    } else if (key == "--stop-degree") {
      options.stop_degree = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--ood-samples") {
      options.ood_samples = stir_whir_gr::bench::ParseUint64(key, value);
    } else if (key == "--format") {
      options.format = stir_whir_gr::bench::ParseOutputFormat(value);
    } else {
      throw std::invalid_argument("unknown option: " + key);
    }
  }
  if (options.d == 0 || options.d >= options.n) {
    throw std::invalid_argument("solver requires 0 < d < n");
  }
  return options;
}

void PrintText(const Options& options,
               const stir_whir_gr::stir::StirTheoremQuerySolveResult& result) {
  std::cout << "protocol=stir9to3\n";
  std::cout << "ring="
            << stir_whir_gr::bench::RingString(options.p, options.k_exp, options.r)
            << "\n";
  std::cout << "n=" << options.n << "\n";
  std::cout << "d=" << options.d << "\n";
  std::cout << "lambda_target=" << options.lambda_target << "\n";
  std::cout << "feasible=" << (result.feasible ? "true" : "false") << "\n";
  std::cout << "effective_security_bits="
            << result.analysis.effective_security_bits << "\n";
  std::cout << "query_schedule=";
  for (std::size_t i = 0; i < result.query_schedule.size(); ++i) {
    if (i != 0) {
      std::cout << ",";
    }
    std::cout << result.query_schedule[i];
  }
  std::cout << "\n";
  for (const auto& note : result.notes) {
    std::cout << "note=" << note << "\n";
  }
  for (const auto& note : result.analysis.assumptions) {
    std::cout << "assumption=" << note << "\n";
  }
}

void PrintJson(const Options& options,
               const stir_whir_gr::stir::StirTheoremQuerySolveResult& result) {
  std::cout << "{\n";
  std::cout << "  \"protocol\": \"stir9to3\",\n";
  std::cout << "  \"ring\": \""
            << stir_whir_gr::bench::JsonEscape(
                   stir_whir_gr::bench::RingString(options.p, options.k_exp, options.r))
            << "\",\n";
  std::cout << "  \"n\": " << options.n << ",\n";
  std::cout << "  \"d\": " << options.d << ",\n";
  std::cout << "  \"lambda_target\": " << options.lambda_target << ",\n";
  std::cout << "  \"feasible\": " << (result.feasible ? "true" : "false")
            << ",\n";
  std::cout << "  \"effective_security_bits\": "
            << result.analysis.effective_security_bits << ",\n";
  std::cout << "  \"query_schedule\": [";
  for (std::size_t i = 0; i < result.query_schedule.size(); ++i) {
    if (i != 0) {
      std::cout << ", ";
    }
    std::cout << result.query_schedule[i];
  }
  std::cout << "],\n";
  std::cout << "  \"notes\": [";
  for (std::size_t i = 0; i < result.notes.size(); ++i) {
    if (i != 0) {
      std::cout << ", ";
    }
    std::cout << "\"" << stir_whir_gr::bench::JsonEscape(result.notes[i]) << "\"";
  }
  std::cout << "],\n";
  std::cout << "  \"assumptions\": [";
  for (std::size_t i = 0; i < result.analysis.assumptions.size(); ++i) {
    if (i != 0) {
      std::cout << ", ";
    }
    std::cout << "\""
              << stir_whir_gr::bench::JsonEscape(result.analysis.assumptions[i]) << "\"";
  }
  std::cout << "]\n";
  std::cout << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = ParseOptions(argc, argv);
    auto ctx = std::make_shared<stir_whir_gr::algebra::GRContext>(stir_whir_gr::algebra::GRConfig{
        .p = options.p,
        .k_exp = options.k_exp,
        .r = options.r,
    });
    (void)ctx->teich_generator();

    stir_whir_gr::stir::StirParameters params;
    params.virtual_fold_factor = 9;
    params.shift_power = 3;
    params.ood_samples = options.ood_samples;
    params.stop_degree = options.stop_degree;
    params.lambda_target = options.lambda_target;
    params.pow_bits = options.pow_bits;
    params.sec_mode = options.sec_mode;
    params.hash_profile = options.hash_profile;
    params.protocol_mode = stir_whir_gr::stir::StirProtocolMode::TheoremGr;
    params.challenge_sampling = stir_whir_gr::stir::StirChallengeSampling::TeichmullerT;
    params.ood_sampling =
        stir_whir_gr::stir::StirOodSamplingMode::TheoremExceptionalComplementUnique;

    const stir_whir_gr::stir::StirInstance instance{
        .domain = stir_whir_gr::Domain::teichmuller_subgroup(ctx, options.n),
        .claimed_degree = options.d,
    };
    const auto result =
        stir_whir_gr::stir::solve_min_query_schedule_for_lambda(params, instance);

    if (options.format == stir_whir_gr::bench::OutputFormat::Json) {
      PrintJson(options, result);
    } else {
      PrintText(options, result);
    }
    return 0;
  } catch (const std::exception& exc) {
    std::cerr << "error: " << exc.what() << "\n";
    return 1;
  }
}
