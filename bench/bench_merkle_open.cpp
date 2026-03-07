#include "bench_common.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "crypto/hash.hpp"
#include "crypto/merkle_tree/merkle_tree.hpp"
#include "crypto/merkle_tree/proof_planner.hpp"

namespace {

constexpr char kLeafDomain[] = "swgr.merkle.leaf.v1";
constexpr char kNodeDomain[] = "swgr.merkle.node.v1";

volatile std::uint64_t g_sink = 0;

enum class QueryMode {
  Spread,
  Adjacent,
  Clustered,
};

struct MerkleOpenBenchOptions {
  std::uint64_t leaves = 1U << 14U;
  std::uint64_t payload_bytes = 288;
  std::uint64_t queries = 64;
  QueryMode query_mode = QueryMode::Adjacent;
  std::uint64_t warmup = 1;
  std::uint64_t reps = 5;
  std::uint64_t iters = 100;
  swgr::HashProfile hash_profile = swgr::HashProfile::STIR_NATIVE;
  swgr::bench::OutputFormat format = swgr::bench::OutputFormat::Text;
};

struct MerkleOpenBenchRow {
  std::string hash_profile;
  std::string query_mode;
  std::uint64_t leaves = 0;
  std::uint64_t padded_leaves = 0;
  std::uint64_t payload_bytes = 0;
  std::uint64_t queries = 0;
  std::uint64_t unique_queries = 0;
  std::uint64_t warmup = 0;
  std::uint64_t reps = 0;
  std::uint64_t iters = 0;
  std::uint64_t digest_bytes = 0;
  std::uint64_t pruned_sibling_hashes = 0;
  std::uint64_t no_pruning_sibling_hashes = 0;
  std::uint64_t pruning_saved_hashes = 0;
  double current_pruned_mean_ms = 0.0;
  double legacy_set_pruned_mean_ms = 0.0;
  double no_pruning_mean_ms = 0.0;
  double current_verify_mean_ms = 0.0;
  double current_open_mean_us = 0.0;
  double legacy_open_mean_us = 0.0;
  double no_pruning_open_mean_us = 0.0;
  double current_verify_mean_us = 0.0;
  double legacy_speedup_x = 0.0;
  double no_pruning_speedup_x = 0.0;
  double sibling_reduction_x = 0.0;
  double legacy_minus_current_ms = 0.0;
  double no_pruning_minus_current_ms = 0.0;
  std::uint64_t checksum_delta = 0;
};

struct BenchMerkleData {
  std::vector<std::vector<std::uint8_t>> leaves;
  std::vector<std::vector<std::vector<std::uint8_t>>> levels;
};

double SafeMean(double total, std::uint64_t reps) {
  return reps == 0 ? 0.0 : total / static_cast<double>(reps);
}

double ElapsedMs(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

double SafeSpeedup(double baseline_ms, double current_ms) {
  return current_ms <= 0.0 ? 0.0 : baseline_ms / current_ms;
}

double MeanUsPerOpen(double mean_ms, std::uint64_t iters) {
  return iters == 0 ? 0.0 : (mean_ms * 1000.0) / static_cast<double>(iters);
}

double SafeRatio(double numerator, double denominator) {
  return denominator <= 0.0 ? 0.0 : numerator / denominator;
}

std::string QueryModeString(QueryMode mode) {
  switch (mode) {
    case QueryMode::Spread:
      return "spread";
    case QueryMode::Adjacent:
      return "adjacent";
    case QueryMode::Clustered:
      return "clustered";
  }
  return "unknown";
}

QueryMode ParseQueryMode(std::string_view raw_value) {
  const std::string normalized = swgr::bench::ToLowerCopy(raw_value);
  if (normalized == "spread") {
    return QueryMode::Spread;
  }
  if (normalized == "adjacent") {
    return QueryMode::Adjacent;
  }
  if (normalized == "clustered") {
    return QueryMode::Clustered;
  }
  throw std::invalid_argument("unknown query-mode: " + std::string(raw_value));
}

std::string Usage(const char* binary_name) {
  return std::string("Usage: ") + binary_name +
         " [options]\n"
         "  --leaves <uint>\n"
         "  --payload-bytes <uint>\n"
         "  --queries <uint>\n"
         "  --query-mode spread|adjacent|clustered\n"
         "  --warmup <uint> --reps <uint> --iters <uint>\n"
         "  --hash-profile STIR_NATIVE|WHIR_NATIVE\n"
         "  --format text|csv|json\n";
}

MerkleOpenBenchOptions ParseOptions(int argc, char** argv) {
  MerkleOpenBenchOptions options;
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

    if (key == "--leaves") {
      options.leaves = swgr::bench::ParseUint64(key, value);
    } else if (key == "--payload-bytes") {
      options.payload_bytes = swgr::bench::ParseUint64(key, value);
    } else if (key == "--queries") {
      options.queries = swgr::bench::ParseUint64(key, value);
    } else if (key == "--query-mode") {
      options.query_mode = ParseQueryMode(value);
    } else if (key == "--warmup") {
      options.warmup = swgr::bench::ParseUint64(key, value);
    } else if (key == "--reps") {
      options.reps = swgr::bench::ParseUint64(key, value);
    } else if (key == "--iters") {
      options.iters = swgr::bench::ParseUint64(key, value);
    } else if (key == "--hash-profile") {
      options.hash_profile = swgr::bench::ParseHashProfile(value);
    } else if (key == "--format") {
      options.format = swgr::bench::ParseOutputFormat(value);
    } else {
      throw std::invalid_argument("unknown option: " + key);
    }
  }

  if (options.leaves == 0) {
    throw std::invalid_argument("--leaves must be > 0");
  }
  if (options.payload_bytes == 0) {
    throw std::invalid_argument("--payload-bytes must be > 0");
  }
  if (options.queries == 0) {
    throw std::invalid_argument("--queries must be > 0");
  }
  if (options.reps == 0) {
    throw std::invalid_argument("--reps must be > 0");
  }
  if (options.iters == 0) {
    throw std::invalid_argument("--iters must be > 0");
  }
  return options;
}

std::uint64_t NextPowerOfTwo(std::uint64_t x) {
  if (x <= 1) {
    return 1;
  }
  std::uint64_t value = 1;
  while (value < x) {
    value <<= 1U;
  }
  return value;
}

void AppendU64Le(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU));
  }
}

std::vector<std::uint8_t> HashLeaf(
    swgr::HashProfile profile, const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> framed;
  framed.reserve(payload.size() + 64);
  framed.insert(framed.end(), kLeafDomain, kLeafDomain + sizeof(kLeafDomain) - 1U);
  AppendU64Le(framed, static_cast<std::uint64_t>(payload.size()));
  framed.insert(framed.end(), payload.begin(), payload.end());
  return swgr::crypto::hash_bytes(profile, swgr::crypto::HashRole::Merkle, framed);
}

std::vector<std::uint8_t> HashParent(
    swgr::HashProfile profile, const std::vector<std::uint8_t>& left,
    const std::vector<std::uint8_t>& right) {
  std::vector<std::uint8_t> framed;
  framed.reserve(left.size() + right.size() + 64);
  framed.insert(framed.end(), kNodeDomain, kNodeDomain + sizeof(kNodeDomain) - 1U);
  AppendU64Le(framed, static_cast<std::uint64_t>(left.size()));
  framed.insert(framed.end(), left.begin(), left.end());
  AppendU64Le(framed, static_cast<std::uint64_t>(right.size()));
  framed.insert(framed.end(), right.begin(), right.end());
  return swgr::crypto::hash_bytes(profile, swgr::crypto::HashRole::Merkle, framed);
}

std::uint64_t DeterministicWord(std::uint64_t a, std::uint64_t b) {
  std::uint64_t x = a * 0x9E3779B97F4A7C15ULL + b * 0xBF58476D1CE4E5B9ULL +
                    0x94D049BB133111EBULL;
  x ^= (x >> 30);
  x *= 0xBF58476D1CE4E5B9ULL;
  x ^= (x >> 27);
  x *= 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

std::vector<std::vector<std::uint8_t>> BuildLeaves(std::uint64_t leaf_count,
                                                   std::uint64_t payload_bytes) {
  std::vector<std::vector<std::uint8_t>> leaves(
      static_cast<std::size_t>(leaf_count),
      std::vector<std::uint8_t>(static_cast<std::size_t>(payload_bytes)));
  for (std::uint64_t leaf_index = 0; leaf_index < leaf_count; ++leaf_index) {
    auto& payload = leaves[static_cast<std::size_t>(leaf_index)];
    for (std::uint64_t offset = 0; offset < payload_bytes; ++offset) {
      const auto word = DeterministicWord(leaf_index + 1U, offset + 17U);
      payload[static_cast<std::size_t>(offset)] =
          static_cast<std::uint8_t>(word & 0xFFU);
    }
  }
  return leaves;
}

std::uint64_t FindCoprimeStep(std::uint64_t modulus, std::uint64_t minimum_step) {
  std::uint64_t step = std::max<std::uint64_t>(1, minimum_step);
  if ((step & 1U) == 0U) {
    ++step;
  }
  while (std::gcd(step, modulus) != 1U) {
    step += 2U;
  }
  return step;
}

std::vector<std::uint64_t> BuildQueries(const MerkleOpenBenchOptions& options) {
  std::vector<std::uint64_t> queries;
  queries.reserve(static_cast<std::size_t>(options.queries));

  switch (options.query_mode) {
    case QueryMode::Spread: {
      const std::uint64_t step = FindCoprimeStep(
          options.leaves, options.leaves / std::max<std::uint64_t>(1, options.queries));
      const std::uint64_t base = options.leaves / 7U;
      for (std::uint64_t i = 0; i < options.queries; ++i) {
        queries.push_back((base + i * step) % options.leaves);
      }
      break;
    }
    case QueryMode::Adjacent: {
      const std::uint64_t base = options.leaves / 3U;
      for (std::uint64_t i = 0; i < options.queries; ++i) {
        queries.push_back((base + i) % options.leaves);
      }
      break;
    }
    case QueryMode::Clustered: {
      const std::uint64_t cluster_size = 8;
      const std::uint64_t span = std::min<std::uint64_t>(options.leaves, 4U);
      const std::uint64_t cluster_count =
          (options.queries + cluster_size - 1U) / cluster_size;
      const std::uint64_t step = FindCoprimeStep(
          options.leaves,
          std::max<std::uint64_t>(span + 1U,
                                  options.leaves /
                                      std::max<std::uint64_t>(1U, cluster_count)));
      const std::uint64_t base = options.leaves / 5U;
      for (std::uint64_t i = 0; i < options.queries; ++i) {
        const std::uint64_t cluster = i / cluster_size;
        const std::uint64_t offset = i % cluster_size;
        queries.push_back((base + cluster * step + (offset % span)) %
                          options.leaves);
      }
      break;
    }
  }
  return queries;
}

std::vector<std::uint64_t> SortAndValidateUnique(
    const std::vector<std::uint64_t>& queried_indices, std::size_t leaf_count) {
  std::vector<std::uint64_t> unique = queried_indices;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  for (const auto index : unique) {
    if (index >= leaf_count) {
      throw std::out_of_range("Merkle query index exceeds leaf count");
    }
  }
  return unique;
}

BenchMerkleData BuildBenchMerkleData(
    swgr::HashProfile profile, std::vector<std::vector<std::uint8_t>> leaves) {
  BenchMerkleData data;
  data.leaves = std::move(leaves);
  const std::size_t padded_leaf_count =
      static_cast<std::size_t>(NextPowerOfTwo(data.leaves.size()));
  data.levels.push_back(
      std::vector<std::vector<std::uint8_t>>(padded_leaf_count));
  auto& leaf_level = data.levels.back();
  for (std::size_t i = 0; i < data.leaves.size(); ++i) {
    leaf_level[i] = HashLeaf(profile, data.leaves[i]);
  }
  for (std::size_t i = data.leaves.size(); i < padded_leaf_count; ++i) {
    leaf_level[i] = leaf_level[data.leaves.size() - 1U];
  }

  while (data.levels.back().size() > 1U) {
    const auto& current = data.levels.back();
    std::vector<std::vector<std::uint8_t>> next(current.size() / 2U);
    for (std::size_t i = 0; i < next.size(); ++i) {
      next[i] = HashParent(profile, current[2U * i], current[2U * i + 1U]);
    }
    data.levels.push_back(std::move(next));
  }
  return data;
}

swgr::crypto::MerkleProof OpenLegacySetPruned(
    const BenchMerkleData& data, const std::vector<std::uint64_t>& queried_indices) {
  swgr::crypto::MerkleProof proof;
  if (data.leaves.empty() || queried_indices.empty()) {
    return proof;
  }

  proof.queried_indices =
      SortAndValidateUnique(queried_indices, data.leaves.size());
  proof.leaf_payloads.reserve(proof.queried_indices.size());
  for (const auto index : proof.queried_indices) {
    proof.leaf_payloads.push_back(data.leaves[static_cast<std::size_t>(index)]);
  }

  std::set<std::uint64_t> current(proof.queried_indices.begin(),
                                  proof.queried_indices.end());
  for (std::size_t level_index = 0; level_index + 1U < data.levels.size();
       ++level_index) {
    std::set<std::uint64_t> parents;
    for (const auto node : current) {
      const auto sibling = node ^ 1ULL;
      if (current.find(sibling) == current.end()) {
        proof.sibling_hashes.push_back(
            data.levels[level_index][static_cast<std::size_t>(sibling)]);
      }
      parents.insert(node / 2ULL);
    }
    current = std::move(parents);
  }
  return proof;
}

swgr::crypto::MerkleProof OpenNoPruningUpperBound(
    const BenchMerkleData& data, const std::vector<std::uint64_t>& queried_indices) {
  swgr::crypto::MerkleProof proof;
  if (data.leaves.empty() || queried_indices.empty()) {
    return proof;
  }

  proof.queried_indices =
      SortAndValidateUnique(queried_indices, data.leaves.size());
  proof.leaf_payloads.reserve(proof.queried_indices.size());
  for (const auto index : proof.queried_indices) {
    proof.leaf_payloads.push_back(data.leaves[static_cast<std::size_t>(index)]);
  }

  for (const auto leaf_index : proof.queried_indices) {
    std::uint64_t node = leaf_index;
    for (std::size_t level_index = 0; level_index + 1U < data.levels.size();
         ++level_index) {
      proof.sibling_hashes.push_back(
          data.levels[level_index][static_cast<std::size_t>(node ^ 1ULL)]);
      node /= 2ULL;
    }
  }
  return proof;
}

void ConsumeProof(const swgr::crypto::MerkleProof& proof) {
  std::uint64_t checksum =
      static_cast<std::uint64_t>(proof.queried_indices.size() * 131U +
                                 proof.leaf_payloads.size() * 17U +
                                 proof.sibling_hashes.size());
  if (!proof.queried_indices.empty()) {
    checksum ^= proof.queried_indices.front();
    checksum ^= proof.queried_indices.back() << 1U;
  }
  if (!proof.sibling_hashes.empty() && !proof.sibling_hashes.front().empty()) {
    checksum ^= static_cast<std::uint64_t>(proof.sibling_hashes.front().front())
                << 7U;
  }
  g_sink = g_sink + checksum + 0x9E3779B97F4A7C15ULL;
}

void ConsumeVerifyResult(bool verified) {
  g_sink = g_sink +
           (verified ? 0xA5A5A5A5A5A5A5A5ULL : 0x5A5A5A5A5A5A5A5AULL);
}

template <typename Fn>
double MeasureMeanMs(std::uint64_t warmup, std::uint64_t reps, Fn&& fn) {
  for (std::uint64_t i = 0; i < warmup; ++i) {
    fn();
  }

  double total_ms = 0.0;
  for (std::uint64_t i = 0; i < reps; ++i) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    total_ms += ElapsedMs(start, end);
  }
  return SafeMean(total_ms, reps);
}

void PrintRowsText(const std::vector<MerkleOpenBenchRow>& rows) {
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    std::cout << "hash_profile=" << row.hash_profile << "\n";
    std::cout << "query_mode=" << row.query_mode << "\n";
    std::cout << "leaves=" << row.leaves << "\n";
    std::cout << "padded_leaves=" << row.padded_leaves << "\n";
    std::cout << "payload_bytes=" << row.payload_bytes << "\n";
    std::cout << "queries=" << row.queries << "\n";
    std::cout << "unique_queries=" << row.unique_queries << "\n";
    std::cout << "warmup=" << row.warmup << "\n";
    std::cout << "reps=" << row.reps << "\n";
    std::cout << "iters=" << row.iters << "\n";
    std::cout << "digest_bytes=" << row.digest_bytes << "\n";
    std::cout << "pruned_sibling_hashes=" << row.pruned_sibling_hashes << "\n";
    std::cout << "no_pruning_sibling_hashes=" << row.no_pruning_sibling_hashes
              << "\n";
    std::cout << "pruning_saved_hashes=" << row.pruning_saved_hashes << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "current_pruned_mean_ms=" << row.current_pruned_mean_ms << "\n"
              << "legacy_set_pruned_mean_ms="
              << row.legacy_set_pruned_mean_ms << "\n"
              << "no_pruning_mean_ms=" << row.no_pruning_mean_ms << "\n"
              << "current_verify_mean_ms=" << row.current_verify_mean_ms << "\n"
              << "current_open_mean_us=" << row.current_open_mean_us << "\n"
              << "legacy_open_mean_us=" << row.legacy_open_mean_us << "\n"
              << "no_pruning_open_mean_us=" << row.no_pruning_open_mean_us
              << "\n"
              << "current_verify_mean_us=" << row.current_verify_mean_us << "\n"
              << "legacy_speedup_x=" << row.legacy_speedup_x << "\n"
              << "no_pruning_speedup_x=" << row.no_pruning_speedup_x << "\n"
              << "sibling_reduction_x=" << row.sibling_reduction_x << "\n"
              << "legacy_minus_current_ms=" << row.legacy_minus_current_ms
              << "\n"
              << "no_pruning_minus_current_ms="
              << row.no_pruning_minus_current_ms << "\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "checksum_delta=" << row.checksum_delta << "\n";
    if (row_index + 1U != rows.size()) {
      std::cout << "\n";
    }
  }
}

void PrintRowsCsv(const std::vector<MerkleOpenBenchRow>& rows) {
  std::cout
      << "hash_profile,query_mode,leaves,padded_leaves,payload_bytes,queries,"
         "unique_queries,warmup,reps,iters,digest_bytes,pruned_sibling_hashes,"
         "no_pruning_sibling_hashes,pruning_saved_hashes,current_pruned_mean_ms,"
         "legacy_set_pruned_mean_ms,no_pruning_mean_ms,current_verify_mean_ms,"
         "current_open_mean_us,legacy_open_mean_us,no_pruning_open_mean_us,"
         "current_verify_mean_us,legacy_speedup_x,no_pruning_speedup_x,"
         "sibling_reduction_x,legacy_minus_current_ms,"
         "no_pruning_minus_current_ms,checksum_delta\n";
  for (const auto& row : rows) {
    std::cout << swgr::bench::CsvEscape(row.hash_profile) << ","
              << swgr::bench::CsvEscape(row.query_mode) << "," << row.leaves
              << "," << row.padded_leaves << "," << row.payload_bytes << ","
              << row.queries << "," << row.unique_queries << "," << row.warmup
              << "," << row.reps << "," << row.iters << "," << row.digest_bytes
              << "," << row.pruned_sibling_hashes << ","
              << row.no_pruning_sibling_hashes << "," << row.pruning_saved_hashes
              << "," << std::fixed << std::setprecision(3)
              << row.current_pruned_mean_ms << ","
              << row.legacy_set_pruned_mean_ms << "," << row.no_pruning_mean_ms
              << "," << row.current_verify_mean_ms
              << "," << row.current_open_mean_us << ","
              << row.legacy_open_mean_us << "," << row.no_pruning_open_mean_us
              << "," << row.current_verify_mean_us << ","
              << row.legacy_speedup_x << "," << row.no_pruning_speedup_x << ","
              << row.sibling_reduction_x << ","
              << row.legacy_minus_current_ms << ","
              << row.no_pruning_minus_current_ms << "," << row.checksum_delta
              << "\n";
    std::cout.unsetf(std::ios::floatfield);
  }
}

void PrintRowsJson(const std::vector<MerkleOpenBenchRow>& rows) {
  std::cout << "[\n";
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    std::cout << "  {\n";
    std::cout << "    \"hash_profile\": \""
              << swgr::bench::JsonEscape(row.hash_profile) << "\",\n";
    std::cout << "    \"query_mode\": \""
              << swgr::bench::JsonEscape(row.query_mode) << "\",\n";
    std::cout << "    \"leaves\": " << row.leaves << ",\n";
    std::cout << "    \"padded_leaves\": " << row.padded_leaves << ",\n";
    std::cout << "    \"payload_bytes\": " << row.payload_bytes << ",\n";
    std::cout << "    \"queries\": " << row.queries << ",\n";
    std::cout << "    \"unique_queries\": " << row.unique_queries << ",\n";
    std::cout << "    \"warmup\": " << row.warmup << ",\n";
    std::cout << "    \"reps\": " << row.reps << ",\n";
    std::cout << "    \"iters\": " << row.iters << ",\n";
    std::cout << "    \"digest_bytes\": " << row.digest_bytes << ",\n";
    std::cout << "    \"pruned_sibling_hashes\": "
              << row.pruned_sibling_hashes << ",\n";
    std::cout << "    \"no_pruning_sibling_hashes\": "
              << row.no_pruning_sibling_hashes << ",\n";
    std::cout << "    \"pruning_saved_hashes\": " << row.pruning_saved_hashes
              << ",\n";
    std::cout << std::fixed << std::setprecision(3)
              << "    \"current_pruned_mean_ms\": "
              << row.current_pruned_mean_ms << ",\n"
              << "    \"legacy_set_pruned_mean_ms\": "
              << row.legacy_set_pruned_mean_ms << ",\n"
              << "    \"no_pruning_mean_ms\": " << row.no_pruning_mean_ms
              << ",\n"
              << "    \"current_verify_mean_ms\": "
              << row.current_verify_mean_ms << ",\n"
              << "    \"current_open_mean_us\": " << row.current_open_mean_us
              << ",\n"
              << "    \"legacy_open_mean_us\": " << row.legacy_open_mean_us
              << ",\n"
              << "    \"no_pruning_open_mean_us\": "
              << row.no_pruning_open_mean_us << ",\n"
              << "    \"current_verify_mean_us\": "
              << row.current_verify_mean_us << ",\n"
              << "    \"legacy_speedup_x\": " << row.legacy_speedup_x << ",\n"
              << "    \"no_pruning_speedup_x\": " << row.no_pruning_speedup_x
              << ",\n"
              << "    \"sibling_reduction_x\": " << row.sibling_reduction_x
              << ",\n"
              << "    \"legacy_minus_current_ms\": "
              << row.legacy_minus_current_ms << ",\n"
              << "    \"no_pruning_minus_current_ms\": "
              << row.no_pruning_minus_current_ms << ",\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "    \"checksum_delta\": " << row.checksum_delta << "\n";
    std::cout << "  }";
    if (row_index + 1U != rows.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "]\n";
}

MerkleOpenBenchRow RunBench(const MerkleOpenBenchOptions& options) {
  const auto leaves = BuildLeaves(options.leaves, options.payload_bytes);
  const auto queries = BuildQueries(options);
  const auto bench_tree = BuildBenchMerkleData(options.hash_profile, leaves);
  const swgr::crypto::MerkleTree tree(options.hash_profile, bench_tree.leaves);
  if (tree.root() != bench_tree.levels.back().front()) {
    throw std::runtime_error("bench_merkle_open tree root mismatch");
  }

  const auto current_proof = tree.open(queries);
  const auto legacy_proof = OpenLegacySetPruned(bench_tree, queries);
  const auto no_pruning_proof = OpenNoPruningUpperBound(bench_tree, queries);
  const auto plan =
      swgr::crypto::build_pruned_multiproof_plan(options.leaves, queries);

  if (legacy_proof.queried_indices != current_proof.queried_indices ||
      legacy_proof.leaf_payloads != current_proof.leaf_payloads ||
      legacy_proof.sibling_hashes != current_proof.sibling_hashes) {
    throw std::runtime_error("bench_merkle_open legacy proof disagrees with current");
  }
  if (current_proof.sibling_hashes.size() !=
      static_cast<std::size_t>(plan.stats.unique_sibling_count)) {
    throw std::runtime_error("bench_merkle_open planner count mismatch");
  }
  if (!swgr::crypto::MerkleTree::verify(options.hash_profile, options.leaves,
                                        tree.root(), current_proof)) {
    throw std::runtime_error("bench_merkle_open current proof failed verification");
  }

  const std::uint64_t checksum_before = g_sink;
  const double current_mean_ms = MeasureMeanMs(options.warmup, options.reps, [&] {
    for (std::uint64_t iter = 0; iter < options.iters; ++iter) {
      ConsumeProof(tree.open(queries));
    }
  });
  const double legacy_mean_ms = MeasureMeanMs(options.warmup, options.reps, [&] {
    for (std::uint64_t iter = 0; iter < options.iters; ++iter) {
      ConsumeProof(OpenLegacySetPruned(bench_tree, queries));
    }
  });
  const double no_pruning_mean_ms =
      MeasureMeanMs(options.warmup, options.reps, [&] {
        for (std::uint64_t iter = 0; iter < options.iters; ++iter) {
          ConsumeProof(OpenNoPruningUpperBound(bench_tree, queries));
        }
      });
  const double verify_mean_ms = MeasureMeanMs(options.warmup, options.reps, [&] {
    for (std::uint64_t iter = 0; iter < options.iters; ++iter) {
      ConsumeVerifyResult(swgr::crypto::MerkleTree::verify(
          options.hash_profile, options.leaves, tree.root(), current_proof));
    }
  });
  const std::uint64_t checksum_after = g_sink;

  MerkleOpenBenchRow row;
  row.hash_profile = swgr::to_string(options.hash_profile);
  row.query_mode = QueryModeString(options.query_mode);
  row.leaves = options.leaves;
  row.padded_leaves = NextPowerOfTwo(options.leaves);
  row.payload_bytes = options.payload_bytes;
  row.queries = options.queries;
  row.unique_queries =
      static_cast<std::uint64_t>(current_proof.queried_indices.size());
  row.warmup = options.warmup;
  row.reps = options.reps;
  row.iters = options.iters;
  row.digest_bytes =
      static_cast<std::uint64_t>(swgr::crypto::digest_bytes(options.hash_profile));
  row.pruned_sibling_hashes =
      static_cast<std::uint64_t>(current_proof.sibling_hashes.size());
  row.no_pruning_sibling_hashes =
      static_cast<std::uint64_t>(no_pruning_proof.sibling_hashes.size());
  row.pruning_saved_hashes =
      row.no_pruning_sibling_hashes - row.pruned_sibling_hashes;
  row.current_pruned_mean_ms = current_mean_ms;
  row.legacy_set_pruned_mean_ms = legacy_mean_ms;
  row.no_pruning_mean_ms = no_pruning_mean_ms;
  row.current_verify_mean_ms = verify_mean_ms;
  row.current_open_mean_us = MeanUsPerOpen(current_mean_ms, options.iters);
  row.legacy_open_mean_us = MeanUsPerOpen(legacy_mean_ms, options.iters);
  row.no_pruning_open_mean_us = MeanUsPerOpen(no_pruning_mean_ms, options.iters);
  row.current_verify_mean_us = MeanUsPerOpen(verify_mean_ms, options.iters);
  row.legacy_speedup_x = SafeSpeedup(legacy_mean_ms, current_mean_ms);
  row.no_pruning_speedup_x = SafeSpeedup(no_pruning_mean_ms, current_mean_ms);
  row.sibling_reduction_x = SafeRatio(
      static_cast<double>(row.no_pruning_sibling_hashes),
      static_cast<double>(row.pruned_sibling_hashes));
  row.legacy_minus_current_ms = legacy_mean_ms - current_mean_ms;
  row.no_pruning_minus_current_ms = no_pruning_mean_ms - current_mean_ms;
  row.checksum_delta = checksum_after - checksum_before;
  return row;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (swgr::bench::WantsHelp(argc, argv)) {
      std::cout << Usage(argv[0]);
      return 0;
    }

    const auto options = ParseOptions(argc, argv);
    const std::vector<MerkleOpenBenchRow> rows = {RunBench(options)};

    switch (options.format) {
      case swgr::bench::OutputFormat::Text:
        PrintRowsText(rows);
        break;
      case swgr::bench::OutputFormat::Csv:
        PrintRowsCsv(rows);
        break;
      case swgr::bench::OutputFormat::Json:
        PrintRowsJson(rows);
        break;
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "bench_merkle_open failed: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "bench_merkle_open failed: unknown exception\n";
    return 1;
  }
}
