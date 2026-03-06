#ifndef GALOISRING_INVERSE_HPP_
#define GALOISRING_INVERSE_HPP_

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pXFactoring.h>
#include <NTL/mat_ZZ.h>
#include <NTL/vec_ZZ_pE.h>
#include <NTL/vector.h>

// Computes the multiplicative inverse of a in the current ZZ_pE context.
// - If s == 1, this is equivalent to NTL::inv(a) in ZZ_p.
// - If a is not a unit (non-invertible), returns 0.
//
// Preconditions:
//   - ZZ_p::init(modulus) has been called (typically modulus = p^k).
//   - ZZ_pE::init(F) has been called with an extension polynomial of degree s.
//
// Call: ZZ_pE a_inv = Inv(a, s);
NTL::ZZ_pE Inv(NTL::ZZ_pE a, long s);

// Computes the inverse of a modulo p^k using a p-adic lifting style algorithm.
//
// Notes:
//   - This function re-initializes ZZ_p::init(...) internally (switching
//     between p, p^2, ..., p^k), but preserves the incoming ZZ_p context (it
//     restores it on return).
//
// Preconditions:
//   - F is the modulus polynomial used to define the ZZ_pE extension (degree
//   s).
//   - p is (typically) prime, k >= 1.
//
// Call: ZZ_pE a_inv = Inv2(a, F, p, s, k);
NTL::ZZ_pE Inv2(NTL::ZZ_pE a, NTL::ZZ_pX F, NTL::ZZ p, long s, long k);

#endif  // GALOISRING_INVERSE_HPP_
