#ifndef STIR_WHIR_GR_STIR_VERIFIER_HPP_
#define STIR_WHIR_GR_STIR_VERIFIER_HPP_

#include "stir/common.hpp"
#include "stir/parameters.hpp"

namespace stir_whir_gr::stir {

class StirVerifier {
 public:
  explicit StirVerifier(StirParameters params);

  bool verify(const StirInstance& instance, const StirProof& proof,
              stir_whir_gr::ProofStatistics* stats = nullptr) const;
  bool verify(const StirInstance& instance, const StirProofWithWitness& artifact,
              stir_whir_gr::ProofStatistics* stats = nullptr) const;

 private:
  StirParameters params_;
};

}  // namespace stir_whir_gr::stir

#endif  // STIR_WHIR_GR_STIR_VERIFIER_HPP_
