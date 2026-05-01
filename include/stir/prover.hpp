#ifndef STIR_WHIR_GR_STIR_PROVER_HPP_
#define STIR_WHIR_GR_STIR_PROVER_HPP_

#include "stir/common.hpp"
#include "stir/parameters.hpp"

namespace stir_whir_gr::stir {

class StirProver {
 public:
  explicit StirProver(StirParameters params);

  StirProof prove(const StirInstance& instance,
                  const stir_whir_gr::poly_utils::Polynomial& polynomial) const;
  StirProofWithWitness prove_with_witness(
      const StirInstance& instance,
      const stir_whir_gr::poly_utils::Polynomial& polynomial) const;

 private:
  StirParameters params_;
};

}  // namespace stir_whir_gr::stir

#endif  // STIR_WHIR_GR_STIR_PROVER_HPP_
