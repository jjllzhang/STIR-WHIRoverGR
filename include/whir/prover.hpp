#ifndef SWGR_WHIR_PROVER_HPP_
#define SWGR_WHIR_PROVER_HPP_

#include <span>

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
  WhirCommitment commit(const WhirPublicParameters& pp,
                        const MultilinearPolynomial& polynomial,
                        WhirCommitmentState* state) const;

  WhirOpening open(const WhirCommitment& commitment,
                   const WhirCommitmentState& state,
                   std::span<const swgr::algebra::GRElem> point) const;

 private:
  WhirParameters params_;
};

}  // namespace swgr::whir

#endif  // SWGR_WHIR_PROVER_HPP_
