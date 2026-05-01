#ifndef STIR_WHIR_GR_BENCH_COMMON_HPP_
#define STIR_WHIR_GR_BENCH_COMMON_HPP_

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

namespace stir_whir_gr::bench {

enum class OutputFormat {
  Text,
  Csv,
  Json,
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

inline stir_whir_gr::SecurityMode ParseSecurityMode(std::string_view raw_value) {
  const std::string normalized = ToLowerCopy(raw_value);
  if (normalized == "conjecturecapacity" ||
      normalized == "conjecture-capacity" ||
      normalized == "conjecture_capacity") {
    return stir_whir_gr::SecurityMode::ConjectureCapacity;
  }
  if (normalized == "conservative") {
    return stir_whir_gr::SecurityMode::Conservative;
  }
  throw std::invalid_argument("unknown sec-mode: " + std::string(raw_value));
}

inline stir_whir_gr::HashProfile ParseHashProfile(std::string_view raw_value) {
  const std::string normalized = ToLowerCopy(raw_value);
  if (normalized == "stir_native" || normalized == "stir-native" ||
      normalized == "stirnative") {
    return stir_whir_gr::HashProfile::STIR_NATIVE;
  }
  if (normalized == "whir_native" || normalized == "whir-native" ||
      normalized == "whirnative") {
    return stir_whir_gr::HashProfile::WHIR_NATIVE;
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
    return {"fri3", "fri9", "stir9to3", "whir_gr_ud"};
  }

  std::vector<std::string> protocols;
  std::string owned(normalized);
  std::size_t start = 0;
  while (start <= owned.size()) {
    const std::size_t comma = owned.find(',', start);
    std::string token = owned.substr(start, comma - start);
    if (token != "fri3" && token != "fri9" && token != "stir9to3" &&
        token != "whir_gr_ud") {
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

}  // namespace stir_whir_gr::bench

#endif  // STIR_WHIR_GR_BENCH_COMMON_HPP_
