#ifndef SWGR_BENCH_COMMON_HPP_
#define SWGR_BENCH_COMMON_HPP_

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "parameters.hpp"

namespace swgr::bench {

enum class OutputFormat {
  Text,
  Csv,
  Json,
};

struct ProofSizeBenchOptions {
  std::vector<std::string> protocols = {"fri3", "fri9", "stir9to3"};
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 162;
  std::uint64_t n = 243;
  std::uint64_t d = 81;
  std::uint64_t lambda_target = 128;
  std::uint64_t pow_bits = 22;
  swgr::SecurityMode sec_mode = swgr::SecurityMode::ConjectureCapacity;
  swgr::HashProfile hash_profile = swgr::HashProfile::STIR_NATIVE;
  std::uint64_t stop_degree = 9;
  std::uint64_t ood_samples = 2;
  std::vector<std::uint64_t> queries;
  std::uint64_t threads = 1;
  OutputFormat format = OutputFormat::Text;
};

struct ProofSizeBenchRow {
  std::string protocol;
  std::string ring;
  std::uint64_t n = 0;
  std::uint64_t d = 0;
  std::string rho;
  std::uint64_t lambda_target = 0;
  std::uint64_t pow_bits = 0;
  std::string sec_mode;
  std::string hash_profile;
  std::string soundness_model;
  std::string query_policy;
  std::string pow_policy;
  std::uint64_t effective_security_bits = 0;
  std::string soundness_notes;
  std::uint64_t fold = 0;
  std::uint64_t shift_power = 0;
  std::uint64_t stop_degree = 0;
  std::uint64_t ood_samples = 0;
  std::uint64_t estimated_argument_bytes = 0;
  double estimated_argument_kib = 0.0;
  std::uint64_t estimated_verifier_hashes = 0;
  std::uint64_t transcript_challenge_count = 0;
  std::uint64_t transcript_bytes_estimated = 0;
  std::uint64_t pow_nonce_bytes = 0;
  std::string round_breakdown_json;
};

inline std::string ToLowerCopy(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return lowered;
}

inline std::string RingString(std::uint64_t p, std::uint64_t k_exp,
                              std::uint64_t r) {
  return "GR(" + std::to_string(p) + "^" + std::to_string(k_exp) + "," +
         std::to_string(r) + ")";
}

inline std::string ReducedRatioString(std::uint64_t numerator,
                                      std::uint64_t denominator) {
  if (denominator == 0) {
    return "undefined";
  }
  const std::uint64_t divisor = std::gcd(numerator, denominator);
  return std::to_string(numerator / divisor) + "/" +
         std::to_string(denominator / divisor);
}

inline std::uint64_t ParseUint64(std::string_view flag_name,
                                 std::string_view raw_value) {
  const std::string owned(raw_value);
  std::size_t parsed = 0;
  const unsigned long long value = std::stoull(owned, &parsed, 10);
  if (parsed != owned.size()) {
    throw std::invalid_argument("invalid integer for " + std::string(flag_name) +
                                ": " + owned);
  }
  return static_cast<std::uint64_t>(value);
}

inline std::vector<std::uint64_t> ParseQueries(std::string_view raw_value) {
  if (raw_value.empty()) {
    return {};
  }

  const std::string normalized = ToLowerCopy(raw_value);
  if (normalized == "auto" || normalized == "default") {
    return {};
  }

  std::vector<std::uint64_t> queries;
  std::string owned(raw_value);
  std::size_t start = 0;
  while (start <= owned.size()) {
    const std::size_t comma = owned.find(',', start);
    const std::string token = owned.substr(start, comma - start);
    if (token.empty()) {
      throw std::invalid_argument("queries must not contain empty items");
    }
    queries.push_back(ParseUint64("--queries", token));
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return queries;
}

inline swgr::SecurityMode ParseSecurityMode(std::string_view raw_value) {
  const std::string normalized = ToLowerCopy(raw_value);
  if (normalized == "conjecturecapacity" ||
      normalized == "conjecture-capacity" ||
      normalized == "conjecture_capacity") {
    return swgr::SecurityMode::ConjectureCapacity;
  }
  if (normalized == "conservative") {
    return swgr::SecurityMode::Conservative;
  }
  throw std::invalid_argument("unknown sec-mode: " + std::string(raw_value));
}

inline swgr::HashProfile ParseHashProfile(std::string_view raw_value) {
  const std::string normalized = ToLowerCopy(raw_value);
  if (normalized == "stir_native" || normalized == "stir-native" ||
      normalized == "stirnative") {
    return swgr::HashProfile::STIR_NATIVE;
  }
  if (normalized == "whir_native" || normalized == "whir-native" ||
      normalized == "whirnative") {
    return swgr::HashProfile::WHIR_NATIVE;
  }
  throw std::invalid_argument("unknown hash-profile: " + std::string(raw_value));
}

inline OutputFormat ParseOutputFormat(std::string_view raw_value) {
  const std::string normalized = ToLowerCopy(raw_value);
  if (normalized == "text") {
    return OutputFormat::Text;
  }
  if (normalized == "csv") {
    return OutputFormat::Csv;
  }
  if (normalized == "json") {
    return OutputFormat::Json;
  }
  throw std::invalid_argument("unknown format: " + std::string(raw_value));
}

inline std::vector<std::string> ParseProtocols(std::string_view raw_value) {
  const std::string normalized = ToLowerCopy(raw_value);
  if (normalized == "all") {
    return {"fri3", "fri9", "stir9to3"};
  }

  std::vector<std::string> protocols;
  std::string owned(normalized);
  std::size_t start = 0;
  while (start <= owned.size()) {
    const std::size_t comma = owned.find(',', start);
    std::string token = owned.substr(start, comma - start);
    if (token != "fri3" && token != "fri9" && token != "stir9to3") {
      throw std::invalid_argument("unknown protocol: " + token);
    }
    if (std::find(protocols.begin(), protocols.end(), token) == protocols.end()) {
      protocols.push_back(std::move(token));
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (protocols.empty()) {
    throw std::invalid_argument("protocol list must not be empty");
  }
  return protocols;
}

inline bool WantsHelp(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      return true;
    }
  }
  return false;
}

inline std::string ProofSizeBenchUsage(const char* binary_name) {
  std::ostringstream oss;
  oss << "Usage: " << binary_name << " [options]\n"
      << "  --protocol fri3|fri9|stir9to3|all\n"
      << "  --p <uint> --k-exp <uint> --r <uint>\n"
      << "  --n <uint> --d <uint>\n"
      << "  --lambda <uint> --pow-bits <uint>\n"
      << "  --sec-mode ConjectureCapacity|Conservative\n"
      << "  --hash-profile STIR_NATIVE|WHIR_NATIVE\n"
      << "  --stop-degree <uint> --ood-samples <uint>\n"
      << "  --queries auto|q0[,q1,...]\n"
      << "  --format text|csv|json\n";
  return oss.str();
}

inline ProofSizeBenchOptions ParseProofSizeBenchOptions(int argc, char** argv) {
  ProofSizeBenchOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--help" || argument == "-h") {
      continue;
    }

    std::string key;
    std::optional<std::string> value;
    const std::size_t equals = argument.find('=');
    if (equals == std::string::npos) {
      key = argument;
      if (i + 1 >= argc) {
        throw std::invalid_argument("missing value after " + key);
      }
      value = std::string(argv[++i]);
    } else {
      key = argument.substr(0, equals);
      value = argument.substr(equals + 1);
    }

    if (!value.has_value()) {
      throw std::invalid_argument("missing value for " + key);
    }

    if (key == "--protocol") {
      options.protocols = ParseProtocols(*value);
    } else if (key == "--p") {
      options.p = ParseUint64(key, *value);
    } else if (key == "--k-exp") {
      options.k_exp = ParseUint64(key, *value);
    } else if (key == "--r") {
      options.r = ParseUint64(key, *value);
    } else if (key == "--n") {
      options.n = ParseUint64(key, *value);
    } else if (key == "--d") {
      options.d = ParseUint64(key, *value);
    } else if (key == "--lambda") {
      options.lambda_target = ParseUint64(key, *value);
    } else if (key == "--pow-bits") {
      options.pow_bits = ParseUint64(key, *value);
    } else if (key == "--sec-mode") {
      options.sec_mode = ParseSecurityMode(*value);
    } else if (key == "--hash-profile") {
      options.hash_profile = ParseHashProfile(*value);
    } else if (key == "--stop-degree") {
      options.stop_degree = ParseUint64(key, *value);
    } else if (key == "--ood-samples") {
      options.ood_samples = ParseUint64(key, *value);
    } else if (key == "--queries") {
      options.queries = ParseQueries(*value);
    } else if (key == "--threads") {
      options.threads = ParseUint64(key, *value);
    } else if (key == "--format") {
      options.format = ParseOutputFormat(*value);
    } else {
      throw std::invalid_argument("unknown option: " + key);
    }
  }

  if (options.protocols.empty()) {
    throw std::invalid_argument("at least one protocol is required");
  }
  if (options.n == 0) {
    throw std::invalid_argument("--n must be > 0");
  }
  if (options.d >= options.n) {
    throw std::invalid_argument("--d must satisfy d < n");
  }
  if (options.p == 0 || options.k_exp == 0 || options.r == 0) {
    throw std::invalid_argument("ring parameters must be > 0");
  }
  return options;
}

inline std::string CsvEscape(std::string_view field) {
  std::string escaped;
  escaped.reserve(field.size() + 2);
  escaped.push_back('"');
  for (const char ch : field) {
    if (ch == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(ch);
  }
  escaped.push_back('"');
  return escaped;
}

inline std::string JsonEscape(std::string_view field) {
  std::string escaped;
  escaped.reserve(field.size() + 8);
  for (const char ch : field) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

inline void PrintRowsText(const std::vector<ProofSizeBenchRow>& rows) {
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    std::cout << "protocol=" << row.protocol << "\n";
    std::cout << "ring=" << row.ring << "\n";
    std::cout << "n=" << row.n << "\n";
    std::cout << "d=" << row.d << "\n";
    std::cout << "rho=" << row.rho << "\n";
    std::cout << "lambda_target=" << row.lambda_target << "\n";
    std::cout << "pow_bits=" << row.pow_bits << "\n";
    std::cout << "sec_mode=" << row.sec_mode << "\n";
    std::cout << "hash_profile=" << row.hash_profile << "\n";
    std::cout << "soundness_model=" << row.soundness_model << "\n";
    std::cout << "query_policy=" << row.query_policy << "\n";
    std::cout << "pow_policy=" << row.pow_policy << "\n";
    std::cout << "effective_security_bits=" << row.effective_security_bits
              << "\n";
    std::cout << "soundness_notes=" << row.soundness_notes << "\n";
    std::cout << "fold=" << row.fold << "\n";
    std::cout << "shift_power=" << row.shift_power << "\n";
    std::cout << "stop_degree=" << row.stop_degree << "\n";
    std::cout << "ood_samples=" << row.ood_samples << "\n";
    std::cout << "estimated_argument_bytes=" << row.estimated_argument_bytes
              << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "estimated_argument_kib=" << row.estimated_argument_kib << "\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "estimated_verifier_hashes=" << row.estimated_verifier_hashes
              << "\n";
    std::cout << "transcript_challenge_count="
              << row.transcript_challenge_count << "\n";
    std::cout << "transcript_bytes_estimated="
              << row.transcript_bytes_estimated << "\n";
    std::cout << "pow_nonce_bytes=" << row.pow_nonce_bytes << "\n";
    std::cout << "round_breakdown_json=" << row.round_breakdown_json << "\n";
    if (row_index + 1 != rows.size()) {
      std::cout << "\n";
    }
  }
}

inline void PrintRowsCsv(const std::vector<ProofSizeBenchRow>& rows) {
  std::cout
      << "protocol,ring,n,d,rho,lambda_target,pow_bits,sec_mode,hash_profile,"
         "soundness_model,query_policy,pow_policy,effective_security_bits,"
         "soundness_notes,fold,shift_power,stop_degree,ood_samples,"
         "estimated_argument_bytes,"
         "estimated_argument_kib,estimated_verifier_hashes,"
         "transcript_challenge_count,transcript_bytes_estimated,"
         "pow_nonce_bytes,round_breakdown_json\n";
  for (const auto& row : rows) {
    std::cout << CsvEscape(row.protocol) << "," << CsvEscape(row.ring) << ","
              << row.n << "," << row.d << "," << CsvEscape(row.rho) << ","
              << row.lambda_target << "," << row.pow_bits << ","
              << CsvEscape(row.sec_mode) << "," << CsvEscape(row.hash_profile)
              << "," << CsvEscape(row.soundness_model) << ","
              << CsvEscape(row.query_policy) << ","
              << CsvEscape(row.pow_policy) << ","
              << row.effective_security_bits << ","
              << CsvEscape(row.soundness_notes) << "," << row.fold << ","
              << row.shift_power << ","
              << row.stop_degree << "," << row.ood_samples << ","
              << row.estimated_argument_bytes << "," << std::fixed
              << std::setprecision(3) << row.estimated_argument_kib << ","
              << row.estimated_verifier_hashes << ","
              << row.transcript_challenge_count << ","
              << row.transcript_bytes_estimated << "," << row.pow_nonce_bytes
              << ","
              << CsvEscape(row.round_breakdown_json) << "\n";
    std::cout.unsetf(std::ios::floatfield);
  }
}

inline void PrintRowsJson(const std::vector<ProofSizeBenchRow>& rows) {
  std::cout << "[\n";
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    std::cout << "  {\n";
    std::cout << "    \"protocol\": \"" << JsonEscape(row.protocol) << "\",\n";
    std::cout << "    \"ring\": \"" << JsonEscape(row.ring) << "\",\n";
    std::cout << "    \"n\": " << row.n << ",\n";
    std::cout << "    \"d\": " << row.d << ",\n";
    std::cout << "    \"rho\": \"" << JsonEscape(row.rho) << "\",\n";
    std::cout << "    \"lambda_target\": " << row.lambda_target << ",\n";
    std::cout << "    \"pow_bits\": " << row.pow_bits << ",\n";
    std::cout << "    \"sec_mode\": \"" << JsonEscape(row.sec_mode) << "\",\n";
    std::cout << "    \"hash_profile\": \"" << JsonEscape(row.hash_profile)
              << "\",\n";
    std::cout << "    \"soundness_model\": \""
              << JsonEscape(row.soundness_model) << "\",\n";
    std::cout << "    \"query_policy\": \"" << JsonEscape(row.query_policy)
              << "\",\n";
    std::cout << "    \"pow_policy\": \"" << JsonEscape(row.pow_policy)
              << "\",\n";
    std::cout << "    \"effective_security_bits\": "
              << row.effective_security_bits << ",\n";
    std::cout << "    \"soundness_notes\": \""
              << JsonEscape(row.soundness_notes) << "\",\n";
    std::cout << "    \"fold\": " << row.fold << ",\n";
    std::cout << "    \"shift_power\": " << row.shift_power << ",\n";
    std::cout << "    \"stop_degree\": " << row.stop_degree << ",\n";
    std::cout << "    \"ood_samples\": " << row.ood_samples << ",\n";
    std::cout << "    \"estimated_argument_bytes\": "
              << row.estimated_argument_bytes << ",\n";
    std::cout << std::fixed << std::setprecision(3)
              << "    \"estimated_argument_kib\": " << row.estimated_argument_kib
              << ",\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << "    \"estimated_verifier_hashes\": "
              << row.estimated_verifier_hashes << ",\n";
    std::cout << "    \"transcript_challenge_count\": "
              << row.transcript_challenge_count << ",\n";
    std::cout << "    \"transcript_bytes_estimated\": "
              << row.transcript_bytes_estimated << ",\n";
    std::cout << "    \"pow_nonce_bytes\": " << row.pow_nonce_bytes << ",\n";
    std::cout << "    \"round_breakdown_json\": " << row.round_breakdown_json
              << "\n";
    std::cout << "  }";
    if (row_index + 1 != rows.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "]\n";
}

inline void PrintRows(const std::vector<ProofSizeBenchRow>& rows,
                      OutputFormat format) {
  switch (format) {
    case OutputFormat::Text:
      PrintRowsText(rows);
      return;
    case OutputFormat::Csv:
      PrintRowsCsv(rows);
      return;
    case OutputFormat::Json:
      PrintRowsJson(rows);
      return;
  }
}

}  // namespace swgr::bench

#endif  // SWGR_BENCH_COMMON_HPP_
