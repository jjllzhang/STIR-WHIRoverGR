#include "stir/prover.hpp"

#include <utility>

#include "utils.hpp"

namespace swgr::stir {

StirProver::StirProver(StirParameters params) : params_(std::move(params)) {}

StirProof StirProver::prove() const {
  (void)params_;
  throw_unimplemented("stir::StirProver::prove");
}

}  // namespace swgr::stir
