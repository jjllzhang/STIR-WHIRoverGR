#include "whir/prover.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::whir {

WhirProver::WhirProver(WhirParameters params) : params_(std::move(params)) {}

WhirProof WhirProver::prove() const {
  (void)params_;
  throw_unimplemented("whir::WhirProver::prove");
}

}  // namespace swgr::whir
