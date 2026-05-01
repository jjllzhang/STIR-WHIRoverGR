#ifndef STIR_WHIR_GR_WHIR_VERIFIER_HPP_
#define STIR_WHIR_GR_WHIR_VERIFIER_HPP_

#include <span>

#include "whir/common.hpp"
#include "whir/parameters.hpp"

namespace stir_whir_gr::whir {

class WhirVerifier {
 public:
  explicit WhirVerifier(WhirParameters params);

  bool verify(const WhirCommitment& commitment,
              std::span<const stir_whir_gr::algebra::GRElem> point,
              const WhirOpening& opening,
              stir_whir_gr::ProofStatistics* stats = nullptr) const;

 private:
  WhirParameters params_;
};

}  // namespace stir_whir_gr::whir

#endif  // STIR_WHIR_GR_WHIR_VERIFIER_HPP_
