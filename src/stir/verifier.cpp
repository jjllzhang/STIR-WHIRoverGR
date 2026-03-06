#include "stir/verifier.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::stir {

StirVerifier::StirVerifier(StirParameters params) : params_(std::move(params)) {}

bool StirVerifier::verify(const StirProof& proof) const {
  (void)params_;
  (void)proof;
  throw_unimplemented("stir::StirVerifier::verify");
}

}  // namespace swgr::stir
