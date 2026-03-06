#include "stir/proof_size_estimator.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::stir {

StirProofSizeEstimator::StirProofSizeEstimator(StirParameters params)
    : params_(std::move(params)) {}

swgr::EstimateResult StirProofSizeEstimator::estimate() const {
  (void)params_;
  throw_unimplemented("stir::StirProofSizeEstimator::estimate");
}

}  // namespace swgr::stir
