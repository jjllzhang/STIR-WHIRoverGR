#include <iostream>

#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "fri/proof_size_estimator.hpp"
#include "parameters.hpp"

int main() {
  const swgr::algebra::GRContext ctx(
      swgr::algebra::GRConfig{.p = 2, .k_exp = 16, .r = 18});
  const swgr::fri::FriInstance instance{
      .domain = swgr::Domain::teichmuller_subgroup(ctx, 27),
      .claimed_degree = 8,
  };

  swgr::fri::FriParameters params;
  params.fold_factor = 3;
  params.stop_degree = 1;
  params.query_repetitions = {2};
  params.lambda_target = 64;
  params.pow_bits = 0;
  params.sec_mode = swgr::SecurityMode::ConjectureCapacity;
  params.hash_profile = swgr::HashProfile::STIR_NATIVE;

  const swgr::fri::FriProofSizeEstimator estimator(params);
  const auto estimate = estimator.estimate(instance);

  std::cout << "protocol=fri3\n";
  std::cout << "ring=GR(2^16,18)\n";
  std::cout << "n=27\n";
  std::cout << "d=8\n";
  std::cout << "lambda_target=" << params.lambda_target << "\n";
  std::cout << "pow_bits=" << params.pow_bits << "\n";
  std::cout << "sec_mode=" << swgr::to_string(params.sec_mode) << "\n";
  std::cout << "hash_profile=" << swgr::to_string(params.hash_profile) << "\n";
  std::cout << "fold=" << params.fold_factor << "\n";
  std::cout << "stop_degree=" << params.stop_degree << "\n";
  std::cout << "estimated_argument_bytes=" << estimate.argument_bytes << "\n";
  std::cout << "estimated_verifier_hashes=" << estimate.verifier_hashes << "\n";
  std::cout << "round_breakdown_json=" << estimate.round_breakdown_json << "\n";
  return 0;
}
