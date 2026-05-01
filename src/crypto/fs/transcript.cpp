#include "crypto/fs/transcript.hpp"

#include <NTL/ZZ.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "algebra/gr_serialization.hpp"
#include "algebra/teichmuller.hpp"
#include "crypto/hash.hpp"

namespace stir_whir_gr::crypto {
namespace {

constexpr char kInitDomain[] = "stir_whir_gr.fs.init.v1";
constexpr char kAbsorbDomain[] = "stir_whir_gr.fs.absorb.v1";
constexpr char kSqueezeDomain[] = "stir_whir_gr.fs.squeeze.v1";
constexpr char kRatchetDomain[] = "stir_whir_gr.fs.ratchet.v1";

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

std::uint64_t ReadU64Le(std::span<const std::uint8_t> bytes) {
  std::uint64_t value = 0;
  const std::size_t width =
      bytes.size() < sizeof(std::uint64_t) ? bytes.size() : sizeof(std::uint64_t);
  for (std::size_t i = 0; i < width; ++i) {
    value |= static_cast<std::uint64_t>(bytes[i]) << (8U * i);
  }
  return value;
}

}  // namespace

Transcript::Transcript(HashProfile profile) : profile_(profile) {
  const auto init_bytes =
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(
                                        kInitDomain),
                                    sizeof(kInitDomain) - 1U);
  state_ = hash_bytes(profile_, HashRole::Transcript, init_bytes);
}

void Transcript::absorb_bytes(std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> framed;
  framed.reserve(state_.size() + data.size() + 64);
  AppendTaggedString(framed, "domain", kAbsorbDomain);
  AppendTaggedBytes(framed, "state", state_);
  AppendTaggedBytes(framed, "message", data);
  state_ = hash_bytes(profile_, HashRole::Transcript, framed);
}

void Transcript::absorb_labeled_bytes(std::string_view label,
                                      std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> framed;
  framed.reserve(state_.size() + label.size() + data.size() + 96);
  AppendTaggedString(framed, "domain", kAbsorbDomain);
  AppendTaggedBytes(framed, "state", state_);
  AppendTaggedString(framed, "label", label);
  AppendTaggedBytes(framed, "message", data);
  state_ = hash_bytes(profile_, HashRole::Transcript, framed);
}

void Transcript::absorb_ring(const algebra::GRContext& ctx,
                             const algebra::GRElem& x) {
  const auto bytes = algebra::serialize_ring_element(ctx, x);
  absorb_bytes(bytes);
}

void Transcript::absorb_labeled_ring(std::string_view label,
                                     const algebra::GRContext& ctx,
                                     const algebra::GRElem& x) {
  const auto bytes = algebra::serialize_ring_element(ctx, x);
  absorb_labeled_bytes(label, bytes);
}

std::vector<std::uint8_t> Transcript::squeeze_bytes(std::string_view label,
                                                    std::size_t byte_count) {
  std::vector<std::uint8_t> out;
  out.reserve(byte_count);

  std::uint64_t block_counter = 0;
  while (out.size() < byte_count) {
    std::vector<std::uint8_t> framed;
    framed.reserve(state_.size() + label.size() + 96);
    AppendTaggedString(framed, "domain", kSqueezeDomain);
    AppendTaggedBytes(framed, "state", state_);
    AppendTaggedString(framed, "label", label);
    AppendU64Le(framed, squeeze_counter_);
    AppendU64Le(framed, block_counter);
    const auto block = hash_bytes(profile_, HashRole::Transcript, framed);
    const std::size_t remaining = byte_count - out.size();
    const std::size_t chunk = remaining < block.size() ? remaining : block.size();
    out.insert(out.end(), block.begin(), block.begin() + chunk);
    ++block_counter;
  }

  std::vector<std::uint8_t> ratchet;
  ratchet.reserve(state_.size() + out.size() + label.size() + 96);
  AppendTaggedString(ratchet, "domain", kRatchetDomain);
  AppendTaggedBytes(ratchet, "prev_state", state_);
  AppendTaggedString(ratchet, "label", label);
  AppendU64Le(ratchet, squeeze_counter_);
  AppendTaggedBytes(ratchet, "output", out);
  state_ = hash_bytes(profile_, HashRole::Transcript, ratchet);
  ++squeeze_counter_;
  return out;
}

algebra::GRElem Transcript::challenge_ring(const algebra::GRContext& ctx,
                                           std::string_view label) {
  const auto bytes = squeeze_bytes(label, ctx.elem_bytes());
  return algebra::deserialize_ring_element(ctx, bytes);
}

algebra::GRElem Transcript::challenge_teichmuller(
    const algebra::GRContext& ctx, std::string_view label) {
  const NTL::ZZ teich_size = algebra::teichmuller_set_size(ctx);
  if (teich_size <= 0) {
    throw std::invalid_argument(
        "challenge_teichmuller requires a positive Teichmuller set size");
  }

  const long sample_bits = NTL::NumBits(teich_size - 1);
  const std::size_t sample_bytes =
      static_cast<std::size_t>((sample_bits + 7) / 8);
  if (sample_bytes == 0) {
    return algebra::teichmuller_element_by_index(ctx, NTL::ZZ(0));
  }

  NTL::ZZ sample_space(1);
  sample_space <<= static_cast<long>(sample_bytes * 8U);
  const NTL::ZZ limit = sample_space - (sample_space % teich_size);
  while (true) {
    const auto bytes = squeeze_bytes(label, sample_bytes);
    NTL::ZZ candidate;
    NTL::ZZFromBytes(
        candidate, reinterpret_cast<const unsigned char*>(bytes.data()),
        static_cast<long>(bytes.size()));
    if (candidate < limit) {
      return algebra::teichmuller_element_by_index(ctx,
                                                   candidate % teich_size);
    }
  }
}

std::uint64_t Transcript::challenge_index(std::string_view label,
                                          std::uint64_t modulus) {
  if (modulus == 0) {
    throw std::invalid_argument("challenge_index requires modulus > 0");
  }

  const std::uint64_t limit =
      std::numeric_limits<std::uint64_t>::max() -
      (std::numeric_limits<std::uint64_t>::max() % modulus);
  while (true) {
    const auto bytes = squeeze_bytes(label, sizeof(std::uint64_t));
    const auto candidate = ReadU64Le(bytes);
    if (candidate < limit) {
      return candidate % modulus;
    }
  }
}

}  // namespace stir_whir_gr::crypto
