#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "crypto/merkle_tree/merkle_tree.hpp"
#include "domain.hpp"
#include "fri/common.hpp"
#include "parameters.hpp"

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<std::size_t> g_sink{0};

template <typename Fn>
double MeanMilliseconds(std::uint64_t reps, Fn&& fn) {
  const auto start = Clock::now();
  for (std::uint64_t rep = 0; rep < reps; ++rep) {
    fn();
  }
  const auto end = Clock::now();
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             end - start)
             .count() /
         static_cast<double>(reps);
}

void RunCase(const swgr::algebra::GRContext& ctx,
             const std::vector<swgr::algebra::GRElem>& oracle_evals,
             std::uint64_t bundle_size, std::uint64_t reps) {
  const std::uint64_t leaf_count =
      static_cast<std::uint64_t>(oracle_evals.size()) / bundle_size;
  const std::size_t total_payload_bytes =
      static_cast<std::size_t>(oracle_evals.size()) * ctx.elem_bytes();

  const double serialize_ms = MeanMilliseconds(reps, [&] {
    std::size_t bytes = 0;
    for (const auto& value : oracle_evals) {
      bytes += ctx.serialize(value).size();
    }
    g_sink.fetch_add(bytes, std::memory_order_relaxed);
  });

  const double leaves_ms = MeanMilliseconds(reps, [&] {
    auto leaves = swgr::fri::build_oracle_leaves(ctx, oracle_evals, bundle_size);
    g_sink.fetch_add(leaves.size(), std::memory_order_relaxed);
  });

  const auto leaves =
      swgr::fri::build_oracle_leaves(ctx, oracle_evals, bundle_size);
  const double merkle_ms = MeanMilliseconds(reps, [&] {
    swgr::crypto::MerkleTree tree(swgr::HashProfile::STIR_NATIVE, leaves);
    g_sink.fetch_add(tree.root().size(), std::memory_order_relaxed);
  });

  std::cout << "bundle_size=" << bundle_size << "\n"
            << "leaf_count=" << leaf_count << "\n"
            << "total_payload_bytes=" << total_payload_bytes << "\n"
            << "serialize_loop_mean_ms=" << std::fixed << std::setprecision(3)
            << serialize_ms << "\n"
            << "build_oracle_leaves_mean_ms=" << leaves_ms << "\n"
            << "merkle_from_prebuilt_leaves_mean_ms=" << merkle_ms << "\n";
  if (merkle_ms > 0.0) {
    std::cout << "leaf_build_to_merkle_ratio=" << (leaves_ms / merkle_ms) << "\n";
  }
  std::cout << "---\n";
}

}  // namespace

int main() {
  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 162});
  const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, 243);
  const auto oracle_evals = domain.elements();
  const std::uint64_t reps = 20;

  std::cout << "ring=GR(2^16,162)\n"
            << "domain_size=" << domain.size() << "\n"
            << "elem_bytes=" << ctx.elem_bytes() << "\n"
            << "reps=" << reps << "\n"
            << "---\n";

  RunCase(ctx, oracle_evals, 1, reps);
  RunCase(ctx, oracle_evals, 9, reps);

  if (g_sink.load(std::memory_order_relaxed) == 0U) {
    std::cerr << "unexpected zero sink\n";
  }
  return 0;
}
