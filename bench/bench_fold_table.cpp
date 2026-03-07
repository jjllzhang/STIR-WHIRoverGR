#include "bench_common.hpp"

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
#include "poly_utils/folding.hpp"

namespace {

volatile std::uint64_t g_sink = 0;

struct FoldTableBenchOptions {
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 162;
  std::uint64_t n = 243;
  std::uint64_t fold = 9;
  std::uint64_t warmup = 1;
  std::uint64_t reps = 5;
  swgr::bench::OutputFormat format = swgr::bench::OutputFormat::Text;
};

struct FoldTableBenchRow {
  std::string ring;
  std::uint64_t n = 0;
  std::uint64_t folded_n = 0;
  std::uint64_t fold = 0;
  std::uint64_t warmup = 0;
  std::uint64_t reps = 0;
  double mean_ms = 0.0;
  std::uint64_t checksum = 0;
};

double ElapsedMs(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

std::string Usage(const char* binary_name) {
  return std::string("Usage: ") + binary_name +
         " [options]\n"
         "  --p <uint> --k-exp <uint> --r <uint>\n"
         "  --n <uint> --fold <uint>\n"
         "  --warmup <uint> --reps <uint>\n"
         "  --format text|csv|json\n";
}

FoldTableBenchOptions ParseOptions(int argc, char** argv) {
  FoldTableBenchOptions options;
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

    if (key == "--p") {
      options.p = swgr::bench::ParseUint64(key, value);
    } else if (key == "--k-exp") {
      options.k_exp = swgr::bench::ParseUint64(key, value);
    } else if (key == "--r") {
      options.r = swgr::bench::ParseUint64(key, value);
    } else if (key == "--n") {
      options.n = swgr::bench::ParseUint64(key, value);
    } else if (key == "--fold") {
      options.fold = swgr::bench::ParseUint64(key, value);
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

  if (options.p == 0 || options.k_exp == 0 || options.r == 0) {
    throw std::invalid_argument("ring parameters must be > 0");
  }
  if (options.n == 0) {
    throw std::invalid_argument("--n must be > 0");
  }
  if (options.fold == 0 || options.n % options.fold != 0) {
    throw std::invalid_argument("bench_fold_table requires fold dividing n");
  }
  if (options.reps == 0) {
    throw std::invalid_argument("--reps must be > 0");
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

std::vector<swgr::algebra::GRElem> BuildEvals(const swgr::Domain& domain) {
  const auto& ctx = domain.context();
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> evals(static_cast<std::size_t>(domain.size()));
    swgr::algebra::GRElem root_power = ctx.one();
    swgr::algebra::GRElem twist_power = ctx.one();
    const swgr::algebra::GRElem twist = ctx.one() + domain.root();
    for (std::uint64_t i = 0; i < domain.size(); ++i) {
      evals[static_cast<std::size_t>(i)] =
          root_power + twist_power + EncodeUnsigned((i % 29U) + 1U);
      root_power *= domain.root();
      twist_power *= twist;
      if ((i % 7U) == 3U) {
        twist_power += domain.root();
      }
    }
    return evals;
  });
}

swgr::algebra::GRElem BuildAlpha(const swgr::Domain& domain) {
  const auto& ctx = domain.context();
  return ctx.with_ntl_context(
      [&] { return ctx.one() + domain.root() + NTL::power(domain.root(), 17L); });
}

std::uint64_t FoldChecksum(const swgr::algebra::GRContext& ctx,
                           const std::vector<swgr::algebra::GRElem>& folded) {
  const auto mid = folded[folded.size() / 2U];
  const auto first_bytes = ctx.serialize(folded.front());
  const auto mid_bytes = ctx.serialize(mid);
  const auto last_bytes = ctx.serialize(folded.back());

  std::uint64_t checksum = static_cast<std::uint64_t>(folded.size());
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

FoldTableBenchRow RunBench(const FoldTableBenchOptions& options) {
  const swgr::algebra::GRContext ctx(swgr::algebra::GRConfig{
      .p = options.p, .k_exp = options.k_exp, .r = options.r});
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, options.n);
  const auto evals = BuildEvals(domain);
  const auto alpha = BuildAlpha(domain);

  for (std::uint64_t warmup_index = 0; warmup_index < options.warmup;
       ++warmup_index) {
    const auto folded =
        swgr::poly_utils::fold_table_k(domain, evals, options.fold, alpha);
    g_sink ^= FoldChecksum(ctx, folded);
  }

  double total_ms = 0.0;
  std::uint64_t checksum = 0;
  for (std::uint64_t rep = 0; rep < options.reps; ++rep) {
    const auto start = std::chrono::steady_clock::now();
    const auto folded =
        swgr::poly_utils::fold_table_k(domain, evals, options.fold, alpha);
    const auto end = std::chrono::steady_clock::now();
    total_ms += ElapsedMs(start, end);
    checksum ^= FoldChecksum(ctx, folded);
  }

  FoldTableBenchRow row;
  row.ring = swgr::bench::RingString(options.p, options.k_exp, options.r);
  row.n = options.n;
  row.folded_n = options.n / options.fold;
  row.fold = options.fold;
  row.warmup = options.warmup;
  row.reps = options.reps;
  row.mean_ms = total_ms / static_cast<double>(options.reps);
  row.checksum = checksum;
  return row;
}

void PrintText(const FoldTableBenchRow& row) {
  std::cout << "ring=" << row.ring << "\n";
  std::cout << "n=" << row.n << "\n";
  std::cout << "folded_n=" << row.folded_n << "\n";
  std::cout << "fold=" << row.fold << "\n";
  std::cout << "warmup=" << row.warmup << "\n";
  std::cout << "reps=" << row.reps << "\n";
  std::cout << std::fixed << std::setprecision(3) << "mean_ms=" << row.mean_ms
            << "\n";
  std::cout.unsetf(std::ios::floatfield);
  std::cout << "checksum=" << row.checksum << "\n";
}

void PrintCsv(const FoldTableBenchRow& row) {
  std::cout << "ring,n,folded_n,fold,warmup,reps,mean_ms,checksum\n";
  std::cout << swgr::bench::CsvEscape(row.ring) << "," << row.n << ","
            << row.folded_n << "," << row.fold << "," << row.warmup << ","
            << row.reps << "," << std::fixed << std::setprecision(3)
            << row.mean_ms << "," << row.checksum << "\n";
  std::cout.unsetf(std::ios::floatfield);
}

void PrintJson(const FoldTableBenchRow& row) {
  std::cout << "{\n"
            << "  \"ring\": \"" << swgr::bench::JsonEscape(row.ring) << "\",\n"
            << "  \"n\": " << row.n << ",\n"
            << "  \"folded_n\": " << row.folded_n << ",\n"
            << "  \"fold\": " << row.fold << ",\n"
            << "  \"warmup\": " << row.warmup << ",\n"
            << "  \"reps\": " << row.reps << ",\n"
            << std::fixed << std::setprecision(3)
            << "  \"mean_ms\": " << row.mean_ms << ",\n"
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

    const FoldTableBenchOptions options = ParseOptions(argc, argv);
    const FoldTableBenchRow row = RunBench(options);
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
    std::cerr << "bench_fold_table failed: " << ex.what() << "\n";
    return 1;
  }
}
