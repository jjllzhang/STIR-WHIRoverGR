#include "bench_common.hpp"

#include <algorithm>
#include <NTL/ZZ_pE.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "poly_utils/fft3.hpp"
#include "poly_utils/polynomial.hpp"

namespace {

volatile std::uint64_t g_sink = 0;

enum class BenchMode {
  Encode,
  Interpolate,
  Roundtrip,
};

struct Fft3BenchOptions {
  BenchMode mode = BenchMode::Encode;
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 162;
  std::uint64_t n = 243;
  std::uint64_t d = 0;
  std::uint64_t warmup = 1;
  std::uint64_t reps = 5;
  std::uint64_t calls_per_rep = 1;
  swgr::bench::OutputFormat format = swgr::bench::OutputFormat::Text;
};

struct Fft3BenchRow {
  std::string mode;
  std::string ring;
  std::uint64_t n = 0;
  std::uint64_t d = 0;
  std::uint64_t input_size = 0;
  std::uint64_t warmup = 0;
  std::uint64_t reps = 0;
  std::uint64_t calls_per_rep = 0;
  double mean_ms = 0.0;
  double mean_call_ms = 0.0;
  std::uint64_t checksum = 0;
};

double ElapsedMs(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool IsPowerOfThree(std::uint64_t value) {
  if (value == 0) {
    return false;
  }
  while (value % 3U == 0U) {
    value /= 3U;
  }
  return value == 1U;
}

std::string ModeString(BenchMode mode) {
  switch (mode) {
    case BenchMode::Encode:
      return "encode";
    case BenchMode::Interpolate:
      return "interpolate";
    case BenchMode::Roundtrip:
      return "roundtrip";
  }
  throw std::invalid_argument("unknown bench mode");
}

BenchMode ParseMode(std::string_view raw_value) {
  const std::string normalized = swgr::bench::ToLowerCopy(raw_value);
  if (normalized == "encode") {
    return BenchMode::Encode;
  }
  if (normalized == "interpolate") {
    return BenchMode::Interpolate;
  }
  if (normalized == "roundtrip") {
    return BenchMode::Roundtrip;
  }
  throw std::invalid_argument("unknown mode: " + std::string(raw_value));
}

std::string Usage(const char* binary_name) {
  return std::string("Usage: ") + binary_name +
         " [options]\n"
         "  --mode encode|interpolate|roundtrip\n"
         "  --p <uint> --k-exp <uint> --r <uint>\n"
         "  --n <uint> [--d <uint>]\n"
         "  --warmup <uint> --reps <uint> [--calls-per-rep <uint>]\n"
         "  --format text|csv|json\n";
}

Fft3BenchOptions ParseOptions(int argc, char** argv) {
  Fft3BenchOptions options;
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

    if (key == "--mode") {
      options.mode = ParseMode(value);
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
    } else if (key == "--warmup") {
      options.warmup = swgr::bench::ParseUint64(key, value);
    } else if (key == "--reps") {
      options.reps = swgr::bench::ParseUint64(key, value);
    } else if (key == "--calls-per-rep") {
      options.calls_per_rep = swgr::bench::ParseUint64(key, value);
    } else if (key == "--format") {
      options.format = swgr::bench::ParseOutputFormat(value);
    } else {
      throw std::invalid_argument("unknown option: " + key);
    }
  }

  if (options.p == 0 || options.k_exp == 0 || options.r == 0) {
    throw std::invalid_argument("ring parameters must be > 0");
  }
  if (options.n == 0) {
    throw std::invalid_argument("--n must be > 0");
  }
  if (!IsPowerOfThree(options.n)) {
    throw std::invalid_argument("bench_fft3 requires n to be a power of three");
  }
  if (options.d == 0) {
    options.d = std::max<std::uint64_t>(1U, options.n / 3U);
  }
  if (options.d > options.n) {
    throw std::invalid_argument("bench_fft3 requires d <= n");
  }
  if (options.reps == 0) {
    throw std::invalid_argument("--reps must be > 0");
  }
  if (options.calls_per_rep == 0) {
    throw std::invalid_argument("--calls-per-rep must be > 0");
  }
  return options;
}

swgr::algebra::GRElem EncodeUnsigned(std::uint64_t value) {
  swgr::algebra::GRElem result;
  NTL::clear(result);
  swgr::algebra::GRElem addend;
  NTL::set(addend);
  while (value > 0) {
    if ((value & 1U) != 0U) {
      result += addend;
    }
    value >>= 1U;
    if (value != 0) {
      addend += addend;
    }
  }
  return result;
}

std::vector<swgr::algebra::GRElem> BuildCoefficients(const swgr::Domain& domain,
                                                     std::uint64_t coeff_count) {
  const auto& ctx = domain.context();
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> coefficients(coeff_count);
    swgr::algebra::GRElem root_power = ctx.one();
    swgr::algebra::GRElem offset_power = ctx.one();
    const swgr::algebra::GRElem twist = ctx.one() + domain.root();
    swgr::algebra::GRElem twist_power = ctx.one();
    for (std::uint64_t i = 0; i < coeff_count; ++i) {
      coefficients[static_cast<std::size_t>(i)] =
          root_power + offset_power + twist_power +
          EncodeUnsigned((i % 23U) + 1U);
      root_power *= domain.root();
      offset_power *= domain.offset();
      twist_power *= twist;
      if ((i % 5U) == 2U) {
        twist_power += domain.offset();
      }
    }
    return coefficients;
  });
}

std::vector<swgr::algebra::GRElem> BuildEvaluations(const swgr::Domain& domain) {
  const auto& ctx = domain.context();
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> evals(static_cast<std::size_t>(domain.size()));
    swgr::algebra::GRElem root_power = ctx.one();
    swgr::algebra::GRElem offset_power = ctx.one();
    const swgr::algebra::GRElem twist = ctx.one() + domain.root() + domain.offset();
    swgr::algebra::GRElem twist_power = ctx.one();
    for (std::uint64_t i = 0; i < domain.size(); ++i) {
      evals[static_cast<std::size_t>(i)] =
          root_power + offset_power + twist_power +
          EncodeUnsigned((i % 29U) + 1U);
      root_power *= domain.root();
      offset_power *= domain.offset();
      twist_power *= twist;
      if ((i % 7U) == 3U) {
        twist_power += domain.root();
      }
    }
    return evals;
  });
}

std::uint64_t VectorChecksum(const swgr::algebra::GRContext& ctx,
                             const std::vector<swgr::algebra::GRElem>& values) {
  const auto& first = values.front();
  const auto& mid = values[values.size() / 2U];
  const auto& last = values.back();

  const auto first_bytes = ctx.serialize(first);
  const auto mid_bytes = ctx.serialize(mid);
  const auto last_bytes = ctx.serialize(last);

  std::uint64_t checksum = static_cast<std::uint64_t>(values.size());
  for (const auto byte : first_bytes) {
    checksum = (checksum * 131U) ^ byte;
  }
  for (const auto byte : mid_bytes) {
    checksum = (checksum * 131U) ^ byte;
  }
  for (const auto byte : last_bytes) {
    checksum = (checksum * 131U) ^ byte;
  }
  g_sink ^= checksum;
  return checksum;
}

std::vector<swgr::algebra::GRElem> RunModeOnce(
    const Fft3BenchOptions& options, const swgr::Domain& domain,
    const swgr::poly_utils::Polynomial& poly,
    const std::vector<swgr::algebra::GRElem>& evals) {
  switch (options.mode) {
    case BenchMode::Encode:
      return swgr::poly_utils::fft3(domain, poly);
    case BenchMode::Interpolate:
      return swgr::poly_utils::inverse_fft3(domain, evals);
    case BenchMode::Roundtrip:
      return swgr::poly_utils::inverse_fft3(
          domain, swgr::poly_utils::fft3(domain, poly));
  }
  throw std::invalid_argument("unknown bench mode");
}

std::vector<std::vector<swgr::algebra::GRElem>> RunCalls(
    const Fft3BenchOptions& options, const swgr::Domain& domain,
    const swgr::poly_utils::Polynomial& poly,
    const std::vector<swgr::algebra::GRElem>& evals) {
  std::vector<std::vector<swgr::algebra::GRElem>> outputs;
  outputs.reserve(static_cast<std::size_t>(options.calls_per_rep));
  for (std::uint64_t call = 0; call < options.calls_per_rep; ++call) {
    outputs.push_back(RunModeOnce(options, domain, poly, evals));
  }
  return outputs;
}

std::uint64_t ChecksumOutputs(
    const swgr::algebra::GRContext& ctx,
    const std::vector<std::vector<swgr::algebra::GRElem>>& outputs) {
  std::uint64_t checksum = 0;
  for (const auto& output : outputs) {
    checksum ^= VectorChecksum(ctx, output);
  }
  return checksum;
}

Fft3BenchRow RunBench(const Fft3BenchOptions& options) {
  const swgr::algebra::GRContext ctx(swgr::algebra::GRConfig{
      .p = options.p, .k_exp = options.k_exp, .r = options.r});
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, options.n);
  const swgr::poly_utils::Polynomial poly(BuildCoefficients(domain, options.d));
  const auto evals = BuildEvaluations(domain);

  for (std::uint64_t warmup_index = 0; warmup_index < options.warmup;
       ++warmup_index) {
    const auto outputs = RunCalls(options, domain, poly, evals);
    g_sink ^= ChecksumOutputs(ctx, outputs);
  }

  double total_ms = 0.0;
  std::uint64_t checksum = 0;
  for (std::uint64_t rep = 0; rep < options.reps; ++rep) {
    const auto start = std::chrono::steady_clock::now();
    const auto outputs = RunCalls(options, domain, poly, evals);
    const auto end = std::chrono::steady_clock::now();
    total_ms += ElapsedMs(start, end);
    checksum ^= ChecksumOutputs(ctx, outputs);
  }

  Fft3BenchRow row;
  row.mode = ModeString(options.mode);
  row.ring = swgr::bench::RingString(options.p, options.k_exp, options.r);
  row.n = options.n;
  row.d = options.d;
  row.input_size =
      options.mode == BenchMode::Interpolate ? options.n : options.d;
  row.warmup = options.warmup;
  row.reps = options.reps;
  row.calls_per_rep = options.calls_per_rep;
  row.mean_ms = total_ms / static_cast<double>(options.reps);
  row.mean_call_ms = row.mean_ms / static_cast<double>(options.calls_per_rep);
  row.checksum = checksum;
  return row;
}

void PrintText(const Fft3BenchRow& row) {
  std::cout << "mode=" << row.mode << "\n";
  std::cout << "ring=" << row.ring << "\n";
  std::cout << "n=" << row.n << "\n";
  std::cout << "d=" << row.d << "\n";
  std::cout << "input_size=" << row.input_size << "\n";
  std::cout << "warmup=" << row.warmup << "\n";
  std::cout << "reps=" << row.reps << "\n";
  std::cout << "calls_per_rep=" << row.calls_per_rep << "\n";
  std::cout << std::fixed << std::setprecision(3) << "mean_ms=" << row.mean_ms
            << "\n";
  std::cout << "mean_call_ms=" << row.mean_call_ms << "\n";
  std::cout.unsetf(std::ios::floatfield);
  std::cout << "checksum=" << row.checksum << "\n";
}

void PrintCsv(const Fft3BenchRow& row) {
  std::cout << "mode,ring,n,d,input_size,warmup,reps,calls_per_rep,mean_ms,"
               "mean_call_ms,checksum\n";
  std::cout << swgr::bench::CsvEscape(row.mode) << ","
            << swgr::bench::CsvEscape(row.ring) << "," << row.n << ","
            << row.d << "," << row.input_size << "," << row.warmup << ","
            << row.reps << "," << row.calls_per_rep << "," << std::fixed
            << std::setprecision(3) << row.mean_ms << "," << row.mean_call_ms
            << "," << row.checksum << "\n";
  std::cout.unsetf(std::ios::floatfield);
}

void PrintJson(const Fft3BenchRow& row) {
  std::cout << "{\n"
            << "  \"mode\": \"" << swgr::bench::JsonEscape(row.mode) << "\",\n"
            << "  \"ring\": \"" << swgr::bench::JsonEscape(row.ring) << "\",\n"
            << "  \"n\": " << row.n << ",\n"
            << "  \"d\": " << row.d << ",\n"
            << "  \"input_size\": " << row.input_size << ",\n"
            << "  \"warmup\": " << row.warmup << ",\n"
            << "  \"reps\": " << row.reps << ",\n"
            << "  \"calls_per_rep\": " << row.calls_per_rep << ",\n"
            << std::fixed << std::setprecision(3)
            << "  \"mean_ms\": " << row.mean_ms << ",\n"
            << "  \"mean_call_ms\": " << row.mean_call_ms << ",\n"
            << std::defaultfloat
            << "  \"checksum\": " << row.checksum << "\n"
            << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (swgr::bench::WantsHelp(argc, argv)) {
      std::cout << Usage(argv[0]);
      return 0;
    }

    const Fft3BenchOptions options = ParseOptions(argc, argv);
    const Fft3BenchRow row = RunBench(options);
    switch (options.format) {
      case swgr::bench::OutputFormat::Text:
        PrintText(row);
        break;
      case swgr::bench::OutputFormat::Csv:
        PrintCsv(row);
        break;
      case swgr::bench::OutputFormat::Json:
        PrintJson(row);
        break;
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "bench_fft3 failed: " << ex.what() << "\n";
    return 1;
  }
}
