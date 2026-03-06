#include "whir/verifier.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::whir {

WhirVerifier::WhirVerifier(WhirParameters params) : params_(std::move(params)) {}

bool WhirVerifier::verify(const WhirProof& proof) const {
  (void)params_;
  (void)proof;
  throw_unimplemented("whir::WhirVerifier::verify");
}

}  // namespace swgr::whir
