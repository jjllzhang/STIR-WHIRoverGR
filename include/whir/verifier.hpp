#ifndef SWGR_WHIR_VERIFIER_HPP_
#define SWGR_WHIR_VERIFIER_HPP_

#include "whir/common.hpp"
#include "whir/parameters.hpp"

namespace swgr::whir {

class WhirVerifier {
 public:
  explicit WhirVerifier(WhirParameters params);

  bool verify(const WhirProof& proof) const;

 private:
  WhirParameters params_;
};

}  // namespace swgr::whir

#endif  // SWGR_WHIR_VERIFIER_HPP_
