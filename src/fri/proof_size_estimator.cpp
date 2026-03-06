#include "fri/proof_size_estimator.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::fri {

FriProofSizeEstimator::FriProofSizeEstimator(FriParameters params)
    : params_(std::move(params)) {}

swgr::EstimateResult FriProofSizeEstimator::estimate() const {
  (void)params_;
  throw_unimplemented("fri::FriProofSizeEstimator::estimate");
}

}  // namespace swgr::fri
