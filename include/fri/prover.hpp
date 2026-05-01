#ifndef STIR_WHIR_GR_FRI_PROVER_HPP_
#define STIR_WHIR_GR_FRI_PROVER_HPP_

#include "fri/common.hpp"
#include "fri/parameters.hpp"

namespace stir_whir_gr::fri {

class FriProver {
 public:
  explicit FriProver(FriParameters params);

  FriCommitment commit(const FriInstance& instance,
                       const stir_whir_gr::poly_utils::Polynomial& polynomial) const;
  FriOpening open(const FriCommitment& commitment,
                  const stir_whir_gr::poly_utils::Polynomial& polynomial,
                  const stir_whir_gr::algebra::GRElem& alpha) const;

 private:
  FriParameters params_;
};

}  // namespace stir_whir_gr::fri

#endif  // STIR_WHIR_GR_FRI_PROVER_HPP_
