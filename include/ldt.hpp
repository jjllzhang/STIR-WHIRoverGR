#ifndef STIR_WHIR_GR_LDT_HPP_
#define STIR_WHIR_GR_LDT_HPP_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stir_whir_gr {

class CountingSink {
 public:
  void append_byte(std::uint8_t /*byte*/) { ++size_; }

  void append_bytes(std::span<const std::uint8_t> bytes) {
    size_ += static_cast<std::uint64_t>(bytes.size());
  }

  std::uint64_t size() const { return size_; }

 private:
  std::uint64_t size_ = 0;
};

template <typename Sink>
inline void SerializeUint64(Sink& sink, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
    sink.append_byte(static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU));
  }
}

template <typename Sink>
inline void SerializeBytes(Sink& sink, std::span<const std::uint8_t> bytes) {
  SerializeUint64(sink, static_cast<std::uint64_t>(bytes.size()));
  sink.append_bytes(bytes);
}

template <typename Sink>
inline void SerializeUint64Vector(Sink& sink,
                                  std::span<const std::uint64_t> values) {
  SerializeUint64(sink, static_cast<std::uint64_t>(values.size()));
  for (const auto value : values) {
    SerializeUint64(sink, value);
  }
}

template <typename Sink>
inline void SerializeByteVector(
    Sink& sink, std::span<const std::vector<std::uint8_t>> values) {
  SerializeUint64(sink, static_cast<std::uint64_t>(values.size()));
  for (const auto& value : values) {
    SerializeBytes(sink, value);
  }
}

struct ProofStatistics {
  std::uint64_t prover_rounds = 0;
  std::uint64_t verifier_hashes = 0;
  std::uint64_t serialized_bytes = 0;
  double commit_ms = 0.0;
  double prove_query_phase_ms = 0.0;
  double prover_total_ms = 0.0;
  double prover_encode_ms = 0.0;
  double prover_merkle_ms = 0.0;
  double prover_transcript_ms = 0.0;
  double prover_fold_ms = 0.0;
  double prover_interpolate_ms = 0.0;
  double prover_query_open_ms = 0.0;
  double prover_ood_ms = 0.0;
  double prover_answer_ms = 0.0;
  double prover_quotient_ms = 0.0;
  double prover_degree_correction_ms = 0.0;
  double verifier_merkle_ms = 0.0;
  double verifier_transcript_ms = 0.0;
  double verifier_query_phase_ms = 0.0;
  double verifier_algebra_ms = 0.0;
  double verifier_total_ms = 0.0;
};

}  // namespace stir_whir_gr

#endif  // STIR_WHIR_GR_LDT_HPP_
