#include "GaloisRing/Inverse.hpp"

#include <algorithm>
#include <vector>

using NTL::clear;
using NTL::coeff;
using NTL::conv;
using NTL::deg;
using NTL::inv;
using NTL::mat_ZZ;
using NTL::mat_ZZ_p;
using NTL::power;
using NTL::rep;
using NTL::SetCoeff;
using NTL::solve;
using NTL::Vec;
using NTL::vec_ZZ;
using NTL::vec_ZZ_p;
using NTL::XGCD;
using NTL::ZZ;
using NTL::ZZ_p;
using NTL::ZZ_pBak;
using NTL::ZZ_pE;
using NTL::ZZ_pEBak;
using NTL::ZZ_pX;

void Inv_matrix_1(mat_ZZ_p &A, ZZ_pE &a, long d);
void Inv_matrix_2(mat_ZZ_p &A, long d);

namespace {

void ExportPolyCoeffs(const ZZ_pX &poly, std::vector<ZZ> &coeffs, long len) {
  coeffs.assign(static_cast<std::size_t>(len), ZZ(0));
  const long d = deg(poly);
  const long upto = std::min(d, len - 1);
  for (long i = 0; i <= upto; ++i) {
    coeffs[static_cast<std::size_t>(i)] = rep(coeff(poly, i));
  }
}

ZZ_pX ImportPolyCoeffs(const std::vector<ZZ> &coeffs) {
  ZZ_pX poly;
  for (long i = 0; i < static_cast<long>(coeffs.size()); ++i) {
    if (coeffs[static_cast<std::size_t>(i)] != 0) {
      SetCoeff(poly, i, conv<ZZ_p>(coeffs[static_cast<std::size_t>(i)]));
    }
  }
  return poly;
}

bool ExtractPrimePowerFromLongModulus(const ZZ &modulus, ZZ &p, long &k) {
  if (modulus <= 1) return false;
  if (NTL::ProbPrime(modulus)) {
    p = modulus;
    k = 1;
    return true;
  }

  const long max_long_bits = static_cast<long>(8 * sizeof(long) - 2);
  if (NTL::NumBits(modulus) > max_long_bits) return false;

  long m = 0;
  conv(m, modulus);
  if (m <= 1) return false;

  long factor = 0;
  if ((m & 1L) == 0) {
    factor = 2;
  } else {
    for (long d = 3; d <= m / d; d += 2) {
      if (m % d == 0) {
        factor = d;
        break;
      }
    }
  }
  if (factor == 0) return false;

  k = 0;
  while (m % factor == 0) {
    m /= factor;
    ++k;
  }
  if (m != 1) return false;

  p = ZZ(factor);
  return true;
}

ZZ_pE InvMatrixSolve(ZZ_pE a, long s) {
  try {
    mat_ZZ_p A1, A2;
    vec_ZZ x, b;

    x.SetLength(s);
    b.SetLength(s);
    clear(b);
    b[0] = 1;

    Inv_matrix_1(A1, a, s);
    Inv_matrix_2(A2, s);

    ZZ d;

    mat_ZZ AB;
    conv(AB, A1 * A2);

    solve(d, x, AB, b);

    vec_ZZ_p x_p;
    conv(x_p, x);

    ZZ_pX poly;
    for (int i = 0; i < s; i++) {
      SetCoeff(poly, i, x_p[i]);
    }

    ZZ_pE a_inverse;
    conv(a_inverse, poly);

    ZZ_pE m = a * a_inverse;
    ZZ_p r;
    ZZ_pX m_poly = rep(m);
    r = coeff(m_poly, 0);

    ZZ_p r_inverse = inv(r);

    ZZ_pE r_pE;
    conv(r_pE, r_inverse);

    return a_inverse * r_pE;
  } catch (const NTL::InvModErrorObject &e) {
    (void)e;
    ZZ_pE zero;
    clear(zero);
    return zero;
  }
}

bool TryInvViaHensel(ZZ_pE &out, const ZZ_pE &a, long s) {
  if (a == 0) {
    clear(out);
    return true;
  }

  const ZZ modulus = ZZ_p::modulus();
  if (NTL::ProbPrime(modulus)) {
    try {
      out = inv(a);
    } catch (...) {
      clear(out);
    }
    return true;
  }

  ZZ p;
  long k = 0;
  if (!ExtractPrimePowerFromLongModulus(modulus, p, k)) return false;
  if (k <= 1) return false;

  const ZZ_pX &F_current = ZZ_pE::modulus();
  std::vector<ZZ> F_coeffs;
  ExportPolyCoeffs(F_current, F_coeffs, s + 1);

  const ZZ_pX &a_poly_current = rep(a);
  std::vector<ZZ> a_coeffs;
  ExportPolyCoeffs(a_poly_current, a_coeffs, s);

  ZZ_pBak modulus_bak;
  modulus_bak.save();
  ZZ_pEBak extension_bak;
  extension_bak.save();

  try {
    ZZ mod = p;
    ZZ_p::init(mod);

    ZZ_pX F_poly = ImportPolyCoeffs(F_coeffs);
    ZZ_pE::init(F_poly);

    ZZ_pX a_poly = ImportPolyCoeffs(a_coeffs);
    ZZ_pE a_mod_p;
    conv(a_mod_p, a_poly);
    if (a_mod_p == 0) {
      clear(out);
      return true;
    }

    ZZ_pE b;
    try {
      b = inv(a_mod_p);
    } catch (...) {
      clear(out);
      return true;
    }

    std::vector<ZZ> b_coeffs;
    ExportPolyCoeffs(rep(b), b_coeffs, s);

    long lifted_exp = 1;
    while (lifted_exp < k) {
      const long next_exp = std::min(2 * lifted_exp, k);
      for (long t = lifted_exp; t < next_exp; ++t) mod *= p;

      ZZ_p::init(mod);
      ZZ_pX F_next = ImportPolyCoeffs(F_coeffs);
      ZZ_pE::init(F_next);

      ZZ_pE a_cur, b_cur;
      conv(a_cur, ImportPolyCoeffs(a_coeffs));
      conv(b_cur, ImportPolyCoeffs(b_coeffs));

      ZZ_pE one;
      NTL::set(one);
      const ZZ_pE two = one + one;
      const ZZ_pE b_next = b_cur * (two - a_cur * b_cur);
      ExportPolyCoeffs(rep(b_next), b_coeffs, s);

      lifted_exp = next_exp;
    }

    ZZ_p::init(modulus);
    ZZ_pX F_final = ImportPolyCoeffs(F_coeffs);
    ZZ_pE::init(F_final);

    conv(out, ImportPolyCoeffs(b_coeffs));
    ZZ_pE one;
    NTL::set(one);
    if (a * out != one) {
      clear(out);
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

/*
    Internal helper for Inv():
    Builds a d x (2d-1) matrix from the polynomial representation of a.
    The matrix encodes shifted coefficients of a and is used to set up a linear
   system whose solution corresponds to the inverse element's coefficients.
*/
void Inv_matrix_1(mat_ZZ_p &A, ZZ_pE &a, long d) {
  ZZ_pX poly = rep(a);
  A.SetDims(d, 2 * d - 1);
  clear(A);

  for (int i = 0; i < d; i++) {
    int index = i;
    for (int j = 0; j < d; j++) {
      A[i][j + index] = coeff(poly, j);
    }
  }
}

/*
    Internal helper for Inv():
    Builds a (2d-1) x d matrix whose rows contain coefficient vectors of 1, x,
   x^2, ... (mod the ZZ_pE modulus), arranged so that A1*A2 forms a d x d linear
   system.
*/
void Inv_matrix_2(mat_ZZ_p &A, long d) {
  A.SetDims(2 * d - 1, d);
  clear(A);

  for (int i = 0; i < d; i++) {
    A[i][i] = 1;
  }

  ZZ_pX H;
  SetCoeff(H, 1, 1);
  ZZ_pE b;
  conv(b, H);

  ZZ_pE tmp = power(b, d);
  for (int i = d; i < 2 * d - 1; i++) {
    const ZZ_pX &poly = rep(tmp);

    for (int j = 0; j < d; j++) {
      A[i][j] = coeff(poly, j);
    }
    tmp *= b;
  }
}

/*
    Computes the inverse of a in the current ZZ_pE context.
    - If s == 1, this delegates to NTL::inv(a) in ZZ_p.
    - If a is not a unit, returns 0.
    Usage: ZZ_pE a_inv = Inv(a, s);
*/
ZZ_pE Inv(ZZ_pE a, long s) {
  if (a == 0) {
    ZZ_pE zero;
    clear(zero);
    return zero;
  }
  if (s == 1) {
    try {
      return inv(a);
    } catch (...) {
      ZZ_pE zero;
      clear(zero);
      return zero;
    }
  }

  ZZ_pE out;
  if (TryInvViaHensel(out, a, s)) return out;
  return InvMatrixSolve(a, s);
}

/*
    Internal helper for Inv2():
    Reduces each coefficient of a modulo n (treating coefficient reps as
   integers), then converts back to ZZ_p. Usage: ZZ_pX a_mod = ZZpXMod(a,
   power(p, i));
*/
ZZ_pX ZZpXMod(ZZ_pX &a, ZZ n) {
  long d = deg(a);
  ZZ_pX res;
  for (int i = 0; i <= d; i++) {
    ZZ m = rep(coeff(a, i));
    m = m % n;
    ZZ_p m_p;
    conv(m_p, m);
    SetCoeff(res, i, m_p);
  }
  return res;
}

/*
    Internal helper for Inv2():
    Divides each coefficient of a by n (integer division on coefficient reps),
   then converts back to ZZ_p. Usage: ZZ_pX a_div = ZZpXDiv(a, power(p, i));
*/
ZZ_pX ZZpXDiv(ZZ_pX &a, ZZ n) {
  long d = deg(a);
  ZZ_pX res;
  for (int i = 0; i <= d; i++) {
    ZZ m = rep(coeff(a, i));
    m = m / n;
    ZZ_p m_p;
    conv(m_p, m);
    SetCoeff(res, i, m_p);
  }
  return res;
}

/*
    Internal helper for Inv2():
    Multiplies each coefficient of a by n (on integer reps), then converts back
   to ZZ_p. Usage: ZZ_pX a_mul = ZZpXMul(a, power(p, i));
*/
ZZ_pX ZZpXMul(ZZ_pX &a, ZZ n) {
  long d = deg(a);
  ZZ_pX res;
  for (int i = 0; i <= d; i++) {
    ZZ m = rep(coeff(a, i));
    m = m * n;
    ZZ_p m_p;
    conv(m_p, m);
    SetCoeff(res, i, m_p);
  }
  return res;
}

/*
    Internal helper for Inv2():
    Computes the p-adic "digits" a_i of a polynomial a up to precision p^k.
    The output satisfies: a ≡ sum_{i=0}^{k-1} a_i * p^i (mod p^k).
*/
Vec<ZZ_pX> Inv_ai(ZZ_pX a, ZZ p, long s, long k) {
  (void)s;
  Vec<ZZ_pX> res, v1;

  ZZ mod = p;
  for (int i = 1; i <= k; i++) {
    ZZ_pX tmp = ZZpXMod(a, mod);
    v1.append(tmp);
    mod *= p;
  }
  res.append(v1[0]);

  ZZ n = p;
  for (int i = 1; i < k; i++) {
    ZZ_pX b = v1[i] - v1[i - 1];

    ZZ_pX c = ZZpXDiv(b, n);
    res.append(c);
    n *= p;
  }
  return res;
}

/*
    Computes the inverse of a modulo p^k using a p-adic lifting style algorithm.
    Inputs:
      - a: element in the current ZZ_pE context (will use its rep() as a
   polynomial)
      - F: modulus polynomial defining ZZ_pE (degree s)
      - p: base prime
      - s: extension degree (basis size)
      - k: exponent (work modulo p^k)
    Usage: ZZ_pE a_inv = Inv2(a, F, p, s, k);
*/
ZZ_pE Inv2(ZZ_pE a, ZZ_pX F, ZZ p, long s, long k) {
  ZZ_pBak modulus_bak;
  modulus_bak.save();

  ZZ_pX a_x = rep(a);

  Vec<ZZ_pX> a_i = Inv_ai(a_x, p, s, k);

  std::vector<ZZ> p_pows;
  p_pows.resize(static_cast<std::size_t>(k + 1));
  p_pows[0] = ZZ(1);
  for (long i = 1; i <= k; ++i) {
    p_pows[static_cast<std::size_t>(i)] =
        p_pows[static_cast<std::size_t>(i - 1)] * p;
  }

  // cout<<"a_i:"<<a_i<<endl;

  Vec<ZZ_pX> u_i, v_i;

  ZZ_pX d, u_0, v_0;
  ZZ_p::init(p);
  XGCD(d, u_0, v_0, a_i[0], F);
  ZZ_pX t = u_0 / F;
  u_0 = u_0 % F;
  v_0 = v_0 + t * a_i[0];
  u_i.append(u_0);
  v_i.append(v_0);
  // cout<<"u_0:"<<u_0<<"v_0:"<<v_0<<endl;

  for (int i = 2; i < k + 1; i++) {
    const ZZ &mod = p_pows[static_cast<std::size_t>(i)];

    ZZ_p::init(mod);
    int n = i - 1;
    ZZ_pX c;

    // for(int j = 0; j < n; j++){
    //     for(int l = 0; l < n; l++){
    //         ZZ_pX tmp = a_i[j] * u_i[l];
    //         ZZ_pX tmp2 = ZZpXMul(tmp,power(p,j+l));
    //         c = c + tmp2;
    //     }
    // }

    for (int y = 0; y < i - 1; y++) {
      const ZZ &py = p_pows[static_cast<std::size_t>(y)];
      for (int j = 0; j <= y; j++) {
        ZZ_pX tmp = a_i[j] * u_i[y - j];
        ZZ_pX tmp2 = ZZpXMul(tmp, py);
        c = c + tmp2;
      }
    }

    for (int j = 0; j < n; j++) {
      ZZ_pX tmp = F * v_i[j];
      ZZ_pX tmp2 = ZZpXMul(tmp, p_pows[static_cast<std::size_t>(j)]);
      c = c + tmp2;
    }

    ZZ_pX e;
    SetCoeff(e, 0, ZZ_p(1));
    c = c - e;
    ZZ_pX g = ZZpXMod(c, mod);
    ZZ_pX h = ZZpXDiv(g, p_pows[static_cast<std::size_t>(n)]);
    // cout<<"g:"<<g<<endl;
    // cout<<"h:"<<h<<endl;
    ZZ_pX o;

    ZZ_pX d, u, v;
    ZZ_p::init(p);
    for (int j = 0; j < n; j++) {
      ZZ_pX r = u_i[j] * a_i[n - j];
      o = o + r;
    }

    XGCD(d, u, v, a_i[0], F);
    u = u * (-1 * (h + o));
    ZZ_pX t = u / F;
    u = u % F;
    v = v * (-1 * (h + o));
    // cout<<"t"<<t * a_i[0]<<endl;

    // cout<<"o:"<<o<<"h+o:"<<(-1 * (h + o))<<endl;
    v = v + t * a_i[0];
    // cout<<"u:"<<u<<"v:"<<v<<endl;
    u_i.append(u);
    v_i.append(v);
  }

  ZZ_p::init(p_pows[static_cast<std::size_t>(k)]);
  ZZ_pX res;
  // cout<<"u_i:"<<u_i<<"v_i:"<<v_i<<endl;
  for (int i = 0; i < k; i++) {
    ZZ_pX tmp = ZZpXMul(u_i[i], p_pows[static_cast<std::size_t>(i)]);
    res = res + tmp;
  }
  ZZ_pE result;
  conv(result, res);

  return result;
}
