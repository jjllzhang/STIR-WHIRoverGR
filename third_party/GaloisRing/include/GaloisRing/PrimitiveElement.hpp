#ifndef GALOISRING_PRIMITIVEELEMENT_HPP_
#define GALOISRING_PRIMITIVEELEMENT_HPP_

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pX.h>
#include <NTL/ZZ_pXFactoring.h>
#include <NTL/vector.h>

// Returns a candidate "primitive element" for GR(p^k, s)
// (implementation-specific).
//
// Notes:
//   - The extension/modulus polynomial F must be provided by the caller (it
//     determines the GR(p^k, s) extension).
//   - The function re-initializes ZZ_p/ZZ_pE contexts internally, but preserves
//     the incoming contexts (it restores them on return). To use the returned
//     element, make sure the caller has set the matching GR(p^k, s) context.
//
// Call: ZZ_pE alpha = FindPrimitiveElement(p, k, s, F);
NTL::ZZ_pE FindPrimitiveElement(NTL::ZZ p, long k, long s,
                                const NTL::ZZ_pX &F);

// Finds a generator of the Teichmuller subgroup of GR(p^k, s), i.e. an
// element of multiplicative order (p^s - 1).
//
// Notes:
//   - Uses a deterministic candidate first (x^(p^(k-1)) mod F), then random
//     unit sampling with Teichmuller projection if needed.
//   - `max_trials` controls the random search budget and must be positive.
//   - The function re-initializes ZZ_p/ZZ_pE contexts internally, and restores
//     incoming contexts on return.
//
// Call: ZZ_pE g = FindTeichmullerGenerator(p, k, s, F);
NTL::ZZ_pE FindTeichmullerGenerator(NTL::ZZ p, long k, long s,
                                    const NTL::ZZ_pX &F,
                                    long max_trials = 1024);

#endif  // GALOISRING_PRIMITIVEELEMENT_HPP_
