#ifndef SWGR_STIR_PROOF_SIZE_ESTIMATOR_HPP_
#define SWGR_STIR_PROOF_SIZE_ESTIMATOR_HPP_

#include "stir/common.hpp"

namespace swgr::stir {

class StirProofSizeEstimator {
 public:
  explicit StirProofSizeEstimator(StirParameters params);

  swgr::EstimateResult estimate(const StirInstance& instance) const;

 private:
  StirParameters params_;
};

}  // namespace swgr::stir

#endif  // SWGR_STIR_PROOF_SIZE_ESTIMATOR_HPP_
