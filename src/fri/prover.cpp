#include "fri/prover.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::fri {

FriProver::FriProver(FriParameters params) : params_(std::move(params)) {}

FriProof FriProver::prove() const {
  (void)params_;
  throw_unimplemented("fri::FriProver::prove");
}

}  // namespace swgr::fri
