#ifndef STIR_WHIR_GR_WHIR_PROVER_HPP_
#define STIR_WHIR_GR_WHIR_PROVER_HPP_

#include <span>

#include "whir/common.hpp"
#include "whir/multiquadratic.hpp"
#include "whir/parameters.hpp"

namespace stir_whir_gr::whir {

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
                   std::span<const stir_whir_gr::algebra::GRElem> point) const;

 private:
  WhirParameters params_;
};

}  // namespace stir_whir_gr::whir

#endif  // STIR_WHIR_GR_WHIR_PROVER_HPP_
