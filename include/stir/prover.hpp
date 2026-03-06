#ifndef SWGR_STIR_PROVER_HPP_
#define SWGR_STIR_PROVER_HPP_

#include "stir/common.hpp"
#include "stir/parameters.hpp"

namespace swgr::stir {

class StirProver {
 public:
  explicit StirProver(StirParameters params);

  StirProof prove(const StirInstance& instance,
                  const swgr::poly_utils::Polynomial& polynomial) const;

 private:
  StirParameters params_;
};

}  // namespace swgr::stir

#endif  // SWGR_STIR_PROVER_HPP_
