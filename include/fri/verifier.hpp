#ifndef SWGR_FRI_VERIFIER_HPP_
#define SWGR_FRI_VERIFIER_HPP_

#include "fri/common.hpp"
#include "fri/parameters.hpp"

namespace swgr::fri {

class FriVerifier {
 public:
  explicit FriVerifier(FriParameters params);

  bool verify(const FriInstance& instance, const FriProofWithWitness& artifact,
              swgr::ProofStatistics* stats = nullptr) const;

 private:
  FriParameters params_;
};

}  // namespace swgr::fri

#endif  // SWGR_FRI_VERIFIER_HPP_
