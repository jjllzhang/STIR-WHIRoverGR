#ifndef STIR_WHIR_GR_FRI_VERIFIER_HPP_
#define STIR_WHIR_GR_FRI_VERIFIER_HPP_

#include "fri/common.hpp"
#include "fri/parameters.hpp"

namespace stir_whir_gr::fri {

class FriVerifier {
 public:
  explicit FriVerifier(FriParameters params);

  bool verify(const FriCommitment& commitment,
              const stir_whir_gr::algebra::GRElem& alpha,
              const stir_whir_gr::algebra::GRElem& value,
              const FriOpening& opening,
              stir_whir_gr::ProofStatistics* stats = nullptr) const;

 private:
  FriParameters params_;
};

}  // namespace stir_whir_gr::fri

#endif  // STIR_WHIR_GR_FRI_VERIFIER_HPP_
