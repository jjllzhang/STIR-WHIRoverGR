#include "GaloisRing/HenselLift.hpp"

using NTL::DivRem;
using NTL::SetCoeff;
using NTL::XGCD;
using NTL::ZZ;
using NTL::ZZ_p;
using NTL::ZZ_pBak;
using NTL::ZZ_pX;

/*
    Internal helper: performs one Hensel lifting step.

    Given f, g, h over the current modulus such that f ≈ g*h and polynomials s,t
   satisfying s*g + t*h ≈ 1, this computes updated (g_, h_, s_, t_) for the next
   lifting precision.

    This is used by the public HenselLift(...) below and is not intended as a
   stable API.
*/
void HenselLift(ZZ_pX &g_, ZZ_pX &h_, ZZ_pX &s_, ZZ_pX &t_, const ZZ_pX &f,
                const ZZ_pX &g, const ZZ_pX &h, const ZZ_pX &s,
                const ZZ_pX &t) {
  ZZ_pX e = f - g * h;

  ZZ_pX q, r;
  DivRem(q, r, s * e, h);

  g_ = g + t * e + q * g;
  h_ = h + r;

  ZZ_pX b = s * g_ + t * h_ - 1;

  ZZ_pX c, d;
  DivRem(c, d, s * b, h_);

  s_ = s - d;
  t_ = t - t * b - c * g_;

  return;
}

/*
    Hensel-lifts a factor g of f from modulo p to modulo p^(n+1).

    Preconditions:
      - Work starts in Z_p[x] (this function initializes ZZ_p to p internally).
      - g should divide f modulo p and be coprime to h = f/g modulo p for
   standard Hensel lifting.

    Side effects:
      - Re-initializes ZZ_p modulus multiple times (p, p^2, ..., p^(n+1)).
      - The incoming ZZ_p context is restored on return.

    Usage: HenselLift(g_lift, f_mod_p, g_mod_p, p, n);
*/
void HenselLift(ZZ_pX &g_, const ZZ_pX &f, const ZZ_pX &g, const ZZ p, long n) {
  ZZ_pBak modulus_bak;
  modulus_bak.save();

  ZZ_p::init(p);

  ZZ_pX h = f / g;
  ZZ_pX d, s, t;
  XGCD(d, s, t, g, h);

  ZZ_pX g_tmp = g;
  ZZ_pX h_tmp = h;
  ZZ_pX f_tmp = f;
  ZZ mod = p * p;
  for (int i = 0; i < n; i++) {
    ZZ_pX h_, s_, t_;
    ZZ_p::init(mod);
    SetCoeff(f_tmp, 0, -1);
    HenselLift(g_, h_, s_, t_, f_tmp, g_tmp, h_tmp, s, t);
    g_tmp = g_;
    h_tmp = h_;
    s = s_;
    t = t_;
    mod *= p;
  }
  return;
}
