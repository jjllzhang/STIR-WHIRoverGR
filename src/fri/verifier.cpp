#include "fri/verifier.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::fri {

FriVerifier::FriVerifier(FriParameters params) : params_(std::move(params)) {}

bool FriVerifier::verify(const FriProof& proof) const {
  (void)params_;
  (void)proof;
  throw_unimplemented("fri::FriVerifier::verify");
}

}  // namespace swgr::fri
