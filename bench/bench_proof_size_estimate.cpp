#include "bench_common.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "fri/proof_size_estimator.hpp"
#include "stir/proof_size_estimator.hpp"

namespace {

swgr::bench::ProofSizeBenchRow MakeFriRow(
    std::string protocol, const swgr::bench::ProofSizeBenchOptions& options,
    const swgr::algebra::GRContext& ctx, std::uint64_t fold_factor) {
  swgr::fri::FriParameters params;
  params.fold_factor = fold_factor;
  params.stop_degree = options.stop_degree;
  params.query_repetitions = options.queries;
  params.lambda_target = options.lambda_target;
  params.pow_bits = options.pow_bits;
  params.sec_mode = options.sec_mode;
  params.hash_profile = options.hash_profile;

  const swgr::fri::FriInstance instance{
      .domain = swgr::Domain::teichmuller_subgroup(ctx, options.n),
      .claimed_degree = options.d,
  };
  const swgr::fri::FriProofSizeEstimator estimator(params);
  const auto estimate = estimator.estimate(instance);

  swgr::bench::ProofSizeBenchRow row;
  row.protocol = std::move(protocol);
  row.ring = swgr::bench::RingString(options.p, options.k_exp, options.r);
  row.n = options.n;
  row.d = options.d;
  row.rho = swgr::bench::ReducedRatioString(options.d, options.n);
  row.lambda_target = options.lambda_target;
  row.pow_bits = options.pow_bits;
  row.sec_mode = swgr::to_string(options.sec_mode);
  row.hash_profile = swgr::to_string(options.hash_profile);
  row.fold = fold_factor;
  row.shift_power = 0;
  row.stop_degree = options.stop_degree;
  row.ood_samples = 0;
  row.estimated_argument_bytes = estimate.argument_bytes;
  row.estimated_argument_kib =
      static_cast<double>(estimate.argument_bytes) / 1024.0;
  row.estimated_verifier_hashes = estimate.verifier_hashes;
  row.round_breakdown_json = estimate.round_breakdown_json;
  return row;
}

swgr::bench::ProofSizeBenchRow MakeStirRow(
    const swgr::bench::ProofSizeBenchOptions& options,
    const swgr::algebra::GRContext& ctx) {
  swgr::stir::StirParameters params;
  params.virtual_fold_factor = 9;
  params.shift_power = 3;
  params.ood_samples = options.ood_samples;
  params.query_repetitions = options.queries;
  params.stop_degree = options.stop_degree;
  params.lambda_target = options.lambda_target;
  params.pow_bits = options.pow_bits;
  params.sec_mode = options.sec_mode;
  params.hash_profile = options.hash_profile;

  const swgr::stir::StirInstance instance{
      .domain = swgr::Domain::teichmuller_subgroup(ctx, options.n),
      .claimed_degree = options.d,
  };
  const swgr::stir::StirProofSizeEstimator estimator(params);
  const auto estimate = estimator.estimate(instance);

  swgr::bench::ProofSizeBenchRow row;
  row.protocol = "stir9to3";
  row.ring = swgr::bench::RingString(options.p, options.k_exp, options.r);
  row.n = options.n;
  row.d = options.d;
  row.rho = swgr::bench::ReducedRatioString(options.d, options.n);
  row.lambda_target = options.lambda_target;
  row.pow_bits = options.pow_bits;
  row.sec_mode = swgr::to_string(options.sec_mode);
  row.hash_profile = swgr::to_string(options.hash_profile);
  row.fold = 9;
  row.shift_power = 3;
  row.stop_degree = options.stop_degree;
  row.ood_samples = options.ood_samples;
  row.estimated_argument_bytes = estimate.argument_bytes;
  row.estimated_argument_kib =
      static_cast<double>(estimate.argument_bytes) / 1024.0;
  row.estimated_verifier_hashes = estimate.verifier_hashes;
  row.round_breakdown_json = estimate.round_breakdown_json;
  return row;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (swgr::bench::WantsHelp(argc, argv)) {
      std::cout << swgr::bench::ProofSizeBenchUsage(argv[0]);
      return 0;
    }

    const auto options = swgr::bench::ParseProofSizeBenchOptions(argc, argv);
    const swgr::algebra::GRContext ctx(swgr::algebra::GRConfig{
        .p = options.p,
        .k_exp = options.k_exp,
        .r = options.r,
    });

    std::vector<swgr::bench::ProofSizeBenchRow> rows;
    rows.reserve(options.protocols.size());
    for (const auto& protocol : options.protocols) {
      if (protocol == "fri3") {
        rows.push_back(MakeFriRow(protocol, options, ctx, 3));
      } else if (protocol == "fri9") {
        rows.push_back(MakeFriRow(protocol, options, ctx, 9));
      } else if (protocol == "stir9to3") {
        rows.push_back(MakeStirRow(options, ctx));
      } else {
        throw std::invalid_argument("unsupported protocol in dispatch: " +
                                    protocol);
      }
    }

    swgr::bench::PrintRows(rows, options.format);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "bench_proof_size_estimate failed: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "bench_proof_size_estimate failed: unknown exception\n";
    return 1;
  }
}
