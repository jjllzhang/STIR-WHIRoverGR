#ifndef SWGR_STIR_PROOF_SIZE_ESTIMATOR_HPP_
#define SWGR_STIR_PROOF_SIZE_ESTIMATOR_HPP_

#include "ldt.hpp"
#include "stir/parameters.hpp"

namespace swgr::stir {

class StirProofSizeEstimator {
 public:
  explicit StirProofSizeEstimator(StirParameters params);

  swgr::EstimateResult estimate() const;

 private:
  StirParameters params_;
};

}  // namespace swgr::stir

#endif  // SWGR_STIR_PROOF_SIZE_ESTIMATOR_HPP_
