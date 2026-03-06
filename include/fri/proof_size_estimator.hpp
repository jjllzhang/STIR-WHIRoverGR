#ifndef SWGR_FRI_PROOF_SIZE_ESTIMATOR_HPP_
#define SWGR_FRI_PROOF_SIZE_ESTIMATOR_HPP_

#include "fri/parameters.hpp"
#include "ldt.hpp"

namespace swgr::fri {

class FriProofSizeEstimator {
 public:
  explicit FriProofSizeEstimator(FriParameters params);

  swgr::EstimateResult estimate() const;

 private:
  FriParameters params_;
};

}  // namespace swgr::fri

#endif  // SWGR_FRI_PROOF_SIZE_ESTIMATOR_HPP_
