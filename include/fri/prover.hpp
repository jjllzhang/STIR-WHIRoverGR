#ifndef SWGR_FRI_PROVER_HPP_
#define SWGR_FRI_PROVER_HPP_

#include "fri/common.hpp"
#include "fri/parameters.hpp"

namespace swgr::fri {

class FriProver {
 public:
  explicit FriProver(FriParameters params);

  FriProof prove() const;

 private:
  FriParameters params_;
};

}  // namespace swgr::fri

#endif  // SWGR_FRI_PROVER_HPP_
