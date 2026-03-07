#include "bench_common.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "crypto/hash.hpp"

namespace {

volatile std::uint64_t g_sink = 0;

constexpr char kInitDomain[] = "swgr.fs.init.v1";
constexpr char kAbsorbDomain[] = "swgr.fs.absorb.v1";
constexpr char kSqueezeDomain[] = "swgr.fs.squeeze.v1";
constexpr char kRatchetDomain[] = "swgr.fs.ratchet.v1";
constexpr char kLeafDomain[] = "swgr.merkle.leaf.v1";
constexpr char kNodeDomain[] = "swgr.merkle.node.v1";

struct HashBenchOptions {
  std::uint64_t leaf_payload_bytes = 288;
  std::uint64_t transcript_message_bytes = 32;
  std::uint64_t squeeze_output_bytes = 32;
  std::uint64_t warmup = 2;
  std::uint64_t reps = 5;
  std::uint64_t iters = 200000;
  swgr::bench::OutputFormat format = swgr::bench::OutputFormat::Text;
};

struct HashBenchRow {
  std::string backend;
  std::uint64_t leaf_payload_bytes = 0;
  std::uint64_t transcript_message_bytes = 0;
  std::uint64_t squeeze_output_bytes = 0;
  std::uint64_t digest_bytes = 0;
  std::uint64_t warmup = 0;
  std::uint64_t reps = 0;
  std::uint64_t iters = 0;
  double leaf_hash_mean_ns = 0.0;
  double parent_hash_mean_ns = 0.0;
  double transcript_absorb_mean_ns = 0.0;
  double transcript_squeeze_round_mean_ns = 0.0;
  double total_mean_ns = 0.0;
  std::uint64_t checksum = 0;
};

void AppendU64Le(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU));
  }
}

void AppendTaggedBytes(std::vector<std::uint8_t>& out, std::string_view label,
                       std::span<const std::uint8_t> bytes) {
  AppendU64Le(out, static_cast<std::uint64_t>(label.size()));
  out.insert(out.end(), label.begin(), label.end());
  AppendU64Le(out, static_cast<std::uint64_t>(bytes.size()));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

void AppendTaggedString(std::vector<std::uint8_t>& out, std::string_view label,
                        std::string_view value) {
  AppendU64Le(out, static_cast<std::uint64_t>(label.size()));
  out.insert(out.end(), label.begin(), label.end());
  AppendU64Le(out, static_cast<std::uint64_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}

HashBenchOptions ParseOptions(int argc, char** argv) {
  HashBenchOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--help" || argument == "-h") {
      continue;
    }

    std::string key;
    std::string value;
    const auto equals = argument.find('=');
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

    if (key == "--leaf-payload-bytes") {
      options.leaf_payload_bytes = swgr::bench::ParseUint64(key, value);
    } else if (key == "--transcript-message-bytes") {
      options.transcript_message_bytes = swgr::bench::ParseUint64(key, value);
    } else if (key == "--squeeze-output-bytes") {
      options.squeeze_output_bytes = swgr::bench::ParseUint64(key, value);
    } else if (key == "--warmup") {
      options.warmup = swgr::bench::ParseUint64(key, value);
    } else if (key == "--reps") {
      options.reps = swgr::bench::ParseUint64(key, value);
    } else if (key == "--iters") {
      options.iters = swgr::bench::ParseUint64(key, value);
    } else if (key == "--format") {
      options.format = swgr::bench::ParseOutputFormat(value);
    } else {
      throw std::invalid_argument("unknown option: " + key);
    }
  }

  if (options.leaf_payload_bytes == 0 || options.transcript_message_bytes == 0 ||
      options.squeeze_output_bytes == 0 || options.reps == 0 ||
      options.iters == 0) {
    throw std::invalid_argument("all numeric options must be > 0");
  }
  return options;
}

double ElapsedNs(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::nano>(end - start).count();
}

template <typename Fn>
double MeasureMeanNs(std::uint64_t warmup, std::uint64_t reps, std::uint64_t iters,
                     Fn&& fn, std::uint64_t* checksum) {
  for (std::uint64_t i = 0; i < warmup * iters; ++i) {
    g_sink ^= fn(i);
  }

  double total_ns = 0.0;
  std::uint64_t local_checksum = 0;
  for (std::uint64_t rep = 0; rep < reps; ++rep) {
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iters; ++i) {
      local_checksum ^= fn(rep * iters + i);
    }
    const auto end = std::chrono::steady_clock::now();
    total_ns += ElapsedNs(start, end);
  }
  g_sink ^= local_checksum;
  *checksum ^= local_checksum;
  return total_ns / static_cast<double>(reps * iters);
}

std::vector<std::uint8_t> MakeBytes(std::uint64_t size, std::uint8_t seed) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(seed + i * 17U);
  }
  return out;
}

HashBenchRow RunBench(swgr::crypto::HashBackend backend,
                      const HashBenchOptions& options) {
  HashBenchRow row;
  row.backend = swgr::crypto::to_string(backend);
  row.leaf_payload_bytes = options.leaf_payload_bytes;
  row.transcript_message_bytes = options.transcript_message_bytes;
  row.squeeze_output_bytes = options.squeeze_output_bytes;
  row.digest_bytes = swgr::crypto::digest_bytes(backend);
  row.warmup = options.warmup;
  row.reps = options.reps;
  row.iters = options.iters;

  const auto state = swgr::crypto::hash_bytes(
      backend,
      std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t*>(kInitDomain),
          sizeof(kInitDomain) - 1U));
  const auto leaf_payload = MakeBytes(options.leaf_payload_bytes, 0x21U);
  const auto transcript_message =
      MakeBytes(options.transcript_message_bytes, 0x35U);
  const auto squeeze_label = std::string("alpha");
  const auto squeeze_output = MakeBytes(options.squeeze_output_bytes, 0x49U);

  std::vector<std::uint8_t> leaf_frame;
  leaf_frame.reserve(leaf_payload.size() + 64);
  leaf_frame.insert(leaf_frame.end(), kLeafDomain,
                    kLeafDomain + sizeof(kLeafDomain) - 1U);
  AppendU64Le(leaf_frame, static_cast<std::uint64_t>(leaf_payload.size()));
  leaf_frame.insert(leaf_frame.end(), leaf_payload.begin(), leaf_payload.end());

  std::vector<std::uint8_t> parent_frame;
  parent_frame.reserve(2U * state.size() + 64);
  parent_frame.insert(parent_frame.end(), kNodeDomain,
                      kNodeDomain + sizeof(kNodeDomain) - 1U);
  AppendU64Le(parent_frame, static_cast<std::uint64_t>(state.size()));
  parent_frame.insert(parent_frame.end(), state.begin(), state.end());
  AppendU64Le(parent_frame, static_cast<std::uint64_t>(state.size()));
  parent_frame.insert(parent_frame.end(), state.begin(), state.end());

  std::vector<std::uint8_t> absorb_frame;
  absorb_frame.reserve(state.size() + transcript_message.size() + 96);
  AppendTaggedString(absorb_frame, "domain", kAbsorbDomain);
  AppendTaggedBytes(absorb_frame, "state", state);
  AppendTaggedBytes(absorb_frame, "message", transcript_message);

  std::vector<std::uint8_t> squeeze_frame;
  squeeze_frame.reserve(state.size() + squeeze_label.size() + 96);
  AppendTaggedString(squeeze_frame, "domain", kSqueezeDomain);
  AppendTaggedBytes(squeeze_frame, "state", state);
  AppendTaggedString(squeeze_frame, "label", squeeze_label);
  AppendU64Le(squeeze_frame, 0);
  AppendU64Le(squeeze_frame, 0);

  std::vector<std::uint8_t> ratchet_frame;
  ratchet_frame.reserve(state.size() + squeeze_output.size() + squeeze_label.size() +
                        96);
  AppendTaggedString(ratchet_frame, "domain", kRatchetDomain);
  AppendTaggedBytes(ratchet_frame, "prev_state", state);
  AppendTaggedString(ratchet_frame, "label", squeeze_label);
  AppendU64Le(ratchet_frame, 0);
  AppendTaggedBytes(ratchet_frame, "output", squeeze_output);

  row.leaf_hash_mean_ns = MeasureMeanNs(
      options.warmup, options.reps, options.iters,
      [&](std::uint64_t) {
        const auto digest = swgr::crypto::hash_bytes(backend, leaf_frame);
        return static_cast<std::uint64_t>(digest.front());
      },
      &row.checksum);

  row.parent_hash_mean_ns = MeasureMeanNs(
      options.warmup, options.reps, options.iters,
      [&](std::uint64_t) {
        const auto digest = swgr::crypto::hash_bytes(backend, parent_frame);
        return static_cast<std::uint64_t>(digest.front());
      },
      &row.checksum);

  row.transcript_absorb_mean_ns = MeasureMeanNs(
      options.warmup, options.reps, options.iters,
      [&](std::uint64_t) {
        const auto digest = swgr::crypto::hash_bytes(backend, absorb_frame);
        return static_cast<std::uint64_t>(digest.front());
      },
      &row.checksum);

  row.transcript_squeeze_round_mean_ns = MeasureMeanNs(
      options.warmup, options.reps, options.iters,
      [&](std::uint64_t) {
        const auto block = swgr::crypto::hash_bytes(backend, squeeze_frame);
        const auto ratchet = swgr::crypto::hash_bytes(backend, ratchet_frame);
        return static_cast<std::uint64_t>(block.front()) ^
               static_cast<std::uint64_t>(ratchet.front());
      },
      &row.checksum);

  row.total_mean_ns = row.leaf_hash_mean_ns + row.parent_hash_mean_ns +
                      row.transcript_absorb_mean_ns +
                      row.transcript_squeeze_round_mean_ns;
  return row;
}

void PrintText(const std::vector<HashBenchRow>& rows) {
  for (const auto& row : rows) {
    std::cout << "backend=" << row.backend << "\n"
              << "leaf_payload_bytes=" << row.leaf_payload_bytes << "\n"
              << "transcript_message_bytes=" << row.transcript_message_bytes
              << "\n"
              << "squeeze_output_bytes=" << row.squeeze_output_bytes << "\n"
              << "digest_bytes=" << row.digest_bytes << "\n"
              << "warmup=" << row.warmup << "\n"
              << "reps=" << row.reps << "\n"
              << "iters=" << row.iters << "\n"
              << std::fixed << std::setprecision(3)
              << "leaf_hash_mean_ns=" << row.leaf_hash_mean_ns << "\n"
              << "parent_hash_mean_ns=" << row.parent_hash_mean_ns << "\n"
              << "transcript_absorb_mean_ns=" << row.transcript_absorb_mean_ns
              << "\n"
              << "transcript_squeeze_round_mean_ns="
              << row.transcript_squeeze_round_mean_ns << "\n"
              << "total_mean_ns=" << row.total_mean_ns << "\n"
              << "checksum=" << row.checksum << "\n\n";
  }
}

void PrintCsv(const std::vector<HashBenchRow>& rows) {
  std::cout << "backend,leaf_payload_bytes,transcript_message_bytes,"
               "squeeze_output_bytes,digest_bytes,warmup,reps,iters,"
               "leaf_hash_mean_ns,parent_hash_mean_ns,transcript_absorb_mean_ns,"
               "transcript_squeeze_round_mean_ns,total_mean_ns,checksum\n";
  for (const auto& row : rows) {
    std::cout << row.backend << "," << row.leaf_payload_bytes << ","
              << row.transcript_message_bytes << "," << row.squeeze_output_bytes
              << "," << row.digest_bytes << "," << row.warmup << ","
              << row.reps << "," << row.iters << "," << std::fixed
              << std::setprecision(3) << row.leaf_hash_mean_ns << ","
              << row.parent_hash_mean_ns << ","
              << row.transcript_absorb_mean_ns << ","
              << row.transcript_squeeze_round_mean_ns << ","
              << row.total_mean_ns << "," << row.checksum << "\n";
  }
}

void PrintJson(const std::vector<HashBenchRow>& rows) {
  std::cout << "[\n";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i];
    std::cout << "  {\n"
              << "    \"backend\": \"" << row.backend << "\",\n"
              << "    \"leaf_payload_bytes\": " << row.leaf_payload_bytes << ",\n"
              << "    \"transcript_message_bytes\": "
              << row.transcript_message_bytes << ",\n"
              << "    \"squeeze_output_bytes\": " << row.squeeze_output_bytes
              << ",\n"
              << "    \"digest_bytes\": " << row.digest_bytes << ",\n"
              << "    \"warmup\": " << row.warmup << ",\n"
              << "    \"reps\": " << row.reps << ",\n"
              << "    \"iters\": " << row.iters << ",\n"
              << std::fixed << std::setprecision(3)
              << "    \"leaf_hash_mean_ns\": " << row.leaf_hash_mean_ns << ",\n"
              << "    \"parent_hash_mean_ns\": " << row.parent_hash_mean_ns
              << ",\n"
              << "    \"transcript_absorb_mean_ns\": "
              << row.transcript_absorb_mean_ns << ",\n"
              << "    \"transcript_squeeze_round_mean_ns\": "
              << row.transcript_squeeze_round_mean_ns << ",\n"
              << "    \"total_mean_ns\": " << row.total_mean_ns << ",\n"
              << "    \"checksum\": " << row.checksum << "\n"
              << "  }" << (i + 1U == rows.size() ? "\n" : ",\n");
  }
  std::cout << "]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = ParseOptions(argc, argv);
    std::vector<HashBenchRow> rows;
    rows.reserve(1);
    rows.push_back(RunBench(swgr::crypto::HashBackend::Blake3, options));

    switch (options.format) {
      case swgr::bench::OutputFormat::Text:
        PrintText(rows);
        break;
      case swgr::bench::OutputFormat::Csv:
        PrintCsv(rows);
        break;
      case swgr::bench::OutputFormat::Json:
        PrintJson(rows);
        break;
    }
  } catch (const std::exception& ex) {
    std::cerr << "bench_hash_backend failed: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "bench_hash_backend failed: unknown exception\n";
    return 1;
  }
  return 0;
}
