#ifndef SWGR_STIR_VERIFIER_HPP_
#define SWGR_STIR_VERIFIER_HPP_

#include "stir/common.hpp"
#include "stir/parameters.hpp"

namespace swgr::stir {

class StirVerifier {
 public:
  explicit StirVerifier(StirParameters params);

  bool verify(const StirInstance& instance, const StirProof& proof) const;

 private:
  StirParameters params_;
};

}  // namespace swgr::stir

#endif  // SWGR_STIR_VERIFIER_HPP_
