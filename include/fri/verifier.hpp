#ifndef SWGR_FRI_VERIFIER_HPP_
#define SWGR_FRI_VERIFIER_HPP_

#include "fri/common.hpp"
#include "fri/parameters.hpp"

namespace swgr::fri {

class FriVerifier {
 public:
  explicit FriVerifier(FriParameters params);

  bool verify(const FriCommitment& commitment,
              const swgr::algebra::GRElem& alpha,
              const swgr::algebra::GRElem& value,
              const FriOpening& opening,
              swgr::ProofStatistics* stats = nullptr) const;

 private:
  FriParameters params_;
};

}  // namespace swgr::fri

#endif  // SWGR_FRI_VERIFIER_HPP_
