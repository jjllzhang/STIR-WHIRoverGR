#ifndef SWGR_WHIR_PROVER_HPP_
#define SWGR_WHIR_PROVER_HPP_

#include "whir/common.hpp"
#include "whir/multiquadratic.hpp"
#include "whir/parameters.hpp"

namespace swgr::whir {

class WhirProver {
 public:
  explicit WhirProver(WhirParameters params);

  WhirCommitment commit(const WhirPublicParameters& pp,
                        const MultiQuadraticPolynomial& polynomial,
                        WhirCommitmentState* state) const;

  WhirProof prove() const;

 private:
  WhirParameters params_;
};

}  // namespace swgr::whir

#endif  // SWGR_WHIR_PROVER_HPP_
