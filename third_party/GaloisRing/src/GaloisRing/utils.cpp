#include "GaloisRing/utils.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using std::copy;
using std::cout;
using std::endl;
using std::min;
using std::size_t;
using std::string;
using std::stringstream;
using std::vector;

using NTL::add;
using NTL::BuildIrred;
using NTL::clear;
using NTL::coeff;
using NTL::conv;
using NTL::deg;
using NTL::DivRem;
using NTL::interpolate;
using NTL::IsZero;
using NTL::LogicError;
using NTL::mul;
using NTL::power;
using NTL::rep;
using NTL::set;
using NTL::SetCoeff;
using NTL::sub;
using NTL::to_ZZ_p;
using NTL::vec_ZZ_pE;
using NTL::ZZ;
using NTL::ZZ_pE;
using NTL::ZZ_pEX;
using NTL::ZZ_pX;

/*
    Returns all positive divisors of n (in ascending order).
    Complexity: O(n) trial division.
    Usage: vector<long> factors = FindFactor(n);
*/
vector<long> FindFactor(long n) {
  vector<long> factors;

  for (long i = 1; i <= n; ++i) {
    if (n % i == 0) {
      factors.push_back(i);
    }
  }

  return factors;
}

/*
    Converts coefficients (low degree -> high degree) into a ZZ_pX over the
   current ZZ_p modulus. Precondition: ZZ_p::init(modulus) has been called.
    Usage: ZZ_pX f = long2ZZpX({1,2,3});  // 1 + 2*x + 3*x^2
*/
ZZ_pX long2ZZpX(const vector<long> &coefficients) {
  ZZ_pX poly;
  for (size_t i = 0; i < coefficients.size(); i++) {
    SetCoeff(poly, i, to_ZZ_p(coefficients[i]));
  }

  return poly;
}

/*
    Converts coefficients into a ZZ_pE element (via ZZ_pX) reduced modulo the
   current ZZ_pE modulus. Preconditions: ZZ_p::init(modulus) and ZZ_pE::init(F)
   have been called. Usage: ZZ_pE a = long2ZZpE({3,5});  // 3 + 5*x in ZZ_pE
*/
ZZ_pE long2ZZpE(const vector<long> &coefficients) {
  ZZ_pX poly;
  for (size_t i = 0; i < coefficients.size(); i++) {
    SetCoeff(poly, i, to_ZZ_p(coefficients[i]));
  }

  ZZ_pE result;
  conv(result, poly);

  return result;
}

/*
    Returns true iff n is a power of two (and n > 0).
    Usage: if (isPowerOfTwo(n)) { ... }
*/
bool isPowerOfTwo(long n) { return n > 0 && (n & (n - 1)) == 0; }

/*
    Flattens a vec_ZZ_pE into a vector<long> by expanding each element into s
   coefficients. Parameter s is the extension degree (basis size). Usage:
   VeczzpE2Veclong(v, out, s);
*/
void VeczzpE2Veclong(const vec_ZZ_pE &v1, vector<long> &m, long s) {
  m.clear();
  const long len = v1.length();
  m.reserve(static_cast<size_t>(len) * static_cast<size_t>(s));

  for (long i = 0; i < len; i++) {
    const ZZ_pX &polyRep = rep(v1[i]);

    for (long j = 0; j < s; j++) {
      long c;
      conv(c, coeff(polyRep, j));
      m.push_back(c);
    }
  }
}

/*
    Concatenates the decimal representations of entries without any separator.
    Usage: string s = Veclong2String({1,0,23});  // "1023"
*/
string Veclong2String(const std::vector<long> &vec) {
  stringstream ss;
  for (size_t i = 0; i < vec.size(); ++i) {
    ss << vec[i];
  }
  return ss.str();
}

/*
    Expands a ZZ_pE element into a vector<long> of length s (coefficients of
   rep(F)). Parameter s is the extension degree (basis size). Usage:
   ZzpE2Veclong(a, out, s);
*/
void ZzpE2Veclong(const ZZ_pE &F, vector<long> &m, long s) {
  m.clear();
  m.reserve(static_cast<size_t>(s));

  const ZZ_pX &polyRep = rep(F);
  for (long i = 0; i < s; i++) {
    long c;
    conv(c, (coeff(polyRep, i)));
    m.push_back(c);
  }
}

/*
    Converts each ZZ_pE element in v1 into a coefficient string (see
   Veclong2String). Parameter s is the extension degree (basis size). Usage:
   VeczzpE2Vecstring(v, out, s);
*/
void VeczzpE2Vecstring(const vec_ZZ_pE &v1, vector<string> &m, long s) {
  m.clear();
  m.reserve(static_cast<size_t>(v1.length()));
  vector<long> b;
  b.reserve(static_cast<size_t>(s));

  for (long i = 0; i < v1.length(); i++) {
    ZzpE2Veclong(v1[i], b, s);
    string a = Veclong2String(b);
    m.push_back(a);
  }
}

/*
    Extracts ZZ_pX coefficients into vector<long> (degrees 0..deg(F)).
    Usage: ZZpX2long(poly, out);
*/
void ZZpX2long(const ZZ_pX &F, vector<long> &Irred) {
  Irred.clear();
  const long degree = deg(F);
  Irred.reserve(static_cast<size_t>(degree + 1));

  for (long i = 0; i <= degree; i++) {
    long c;
    conv(c, (coeff(F, i)));
    Irred.push_back(c);
  }
}

/*
    Flattens a ZZ_pEX into vector<long> by expanding each ZZ_pE coefficient into
   s longs. Parameter s is the extension degree (basis size). Usage:
   ZZpEX2long(f, out, s);
*/
void ZZpEX2long(const ZZ_pEX &v1, vector<long> &m, long s) {
  m.clear();
  const long degree = deg(v1);
  if (degree < 0) {
    return;
  }
  m.reserve(static_cast<size_t>(degree + 1) * static_cast<size_t>(s));

  for (long i = 0; i <= degree; i++) {
    const ZZ_pE element = coeff(v1, i);
    const ZZ_pX &polyRep = rep(element);

    for (long j = 0; j < s; j++) {
      long c;
      conv(c, coeff(polyRep, j));
      m.push_back(c);
    }
  }
}

/*
    Returns true iff all elements in vec are non-zero.
    Usage: if (allNonZero(v)) { ... }
*/
bool allNonZero(const vec_ZZ_pE &vec) {
  for (const auto &elem : vec) {
    if (elem == 0) {
      return false;
    }
  }
  return true;
}

/*
    Returns the smallest power of two >= n.
    Precondition: n >= 0 (negative inputs are not supported).
    Usage: long m = nextPowerOf2(n);
*/
long nextPowerOf2(long n) {
  long count = 0;

  if (n && !(n & (n - 1)))
    return n;

  while (n != 0) {
    n >>= 1;
    count++;
  }

  return 1 << count;
}

/*
    Prints a vector<long> to stdout using a simple bracketed format.
    Usage: print(v);
*/
void print(vector<long> &vec) {
  cout << "[";
  for (const auto &element : vec) {
    cout << element << " ";
  }
  cout << "]" << endl;
}

/*
    Pads (or truncates) a vector to targetLength by appending zeros.
    Usage: auto padded = PadVectorToLength(v, 16);
*/
vector<long> PadVectorToLength(const vector<long> &input, size_t targetLength) {
  vector<long> paddedVector(targetLength, 0);

  size_t copyLength = min(input.size(), targetLength);
  copy(input.begin(), input.begin() + copyLength, paddedVector.begin());

  return paddedVector;
}

/*
    Removes trailing zeros from a vector.
    Returns an empty vector if all entries are zero.
    Usage: auto trimmed = TrimVector(v);
*/
vector<long> TrimVector(const std::vector<long> &input) {
  size_t trimmedSize = input.size();
  while (trimmedSize > 0 && input[trimmedSize - 1] == 0) {
    trimmedSize--;
  }

  return vector<long>(input.begin(), input.begin() + trimmedSize);
}

/*
    Splits input into numSegments equal-size chunks (using floor division),
    pads each chunk to segmentLength, and concatenates the padded chunks.
    Note: if input.size() is not divisible by numSegments, trailing elements may
   be ignored. Usage: auto out = SplitAndPadVector(v, segmentLength,
   numSegments);
*/
vector<long> SplitAndPadVector(const vector<long> &input, long segmentLength,
                               long numSegments) {
  vector<long> result;
  result.reserve(segmentLength * numSegments);

  long n = input.size() / numSegments;

  for (long i = 0; i < numSegments; ++i) {
    auto segmentStart = input.begin() + i * n;
    const long copyLength = min(segmentLength, n);
    result.insert(result.end(), segmentStart, segmentStart + copyLength);
    if (copyLength < segmentLength) {
      result.insert(result.end(), segmentLength - copyLength, 0);
    }
  }

  return result;
}

/*
    Converts a flattened vector<long> into a ZZ_pEX by grouping every s entries
   as one ZZ_pE coefficient. Precondition: result.size() is a multiple of s;
   ZZ_p/ZZ_pE contexts are initialized. Usage: Long2ZZpEX(flat, poly, s);
*/
void Long2ZZpEX(const vector<long> &result, ZZ_pEX &V, long s) {
  long index = 0;

  long n = result.size() / s;
  for (long j = 0; j < n; j++) {
    ZZ_pX polyRep;
    for (long k = 0; k < s; k++) {
      SetCoeff(polyRep, k, to_ZZ_p(result[index++]));
    }
    ZZ_pE a;
    conv(a, polyRep);

    SetCoeff(V, j, a);
  }
}

/*
    Same as Long2ZZpEX, but only reads n ZZ_pE coefficients from result.
    Precondition: result.size() >= n*s; ZZ_p/ZZ_pE contexts are initialized.
    Usage: Long2ZZpEX2(flat, poly, s, n);
*/
void Long2ZZpEX2(const vector<long> &result, ZZ_pEX &V, long s, long n) {
  long index = 0;

  for (long j = 0; j < n; j++) {
    ZZ_pX polyRep;
    for (long k = 0; k < s; k++) {
      SetCoeff(polyRep, k, to_ZZ_p(result[index++]));
    }
    ZZ_pE a;
    conv(a, polyRep);

    SetCoeff(V, j, a);
  }
}

/*
    Returns the smallest perfect square strictly greater than num.
    Precondition: num >= 0.
    Usage: int sq = nearestPerfectSquare(num);
*/
int nearestPerfectSquare(int num) {
  int root = sqrt(num);

  while (root * root <= num) {
    root++;
  }

  return root * root;
}

/*
    Serializes a ZZ_pX (e.g., an irreducible polynomial) into vector<long>
   coefficients. Usage: fillIrred(F, out);
*/
void fillIrred(const ZZ_pX &F, vector<long> &Irred) {
  Irred.clear();
  const long degree = deg(F);
  Irred.reserve(static_cast<size_t>(degree + 1));

  for (long i = 0; i <= degree; i++) {
    long c;
    conv(c, (coeff(F, i)));
    Irred.push_back(c);
  }
}

/*
    Serializes interpolation points into a flattened vector<long> by expanding
   each ZZ_pE into s coefficients. Usage: fillInterpolation(points, out, s);
*/
void fillInterpolation(const vec_ZZ_pE &v1, vector<long> &Interpolation,
                       long s) {
  Interpolation.clear();
  const long len = v1.length();
  Interpolation.reserve(static_cast<size_t>(len) * static_cast<size_t>(s));

  for (long i = 0; i < len; i++) {
    const ZZ_pX &polyRep = rep(v1[i]);

    for (long j = 0; j < s; j++) {
      long c;
      conv(c, coeff(polyRep, j));
      Interpolation.push_back(c);
    }
  }
}

/*
    Attempts to find a degree-n polynomial in Z_p[x] intended to be primitive.
    Precondition: ZZ_p::init(p) has been called by the caller.
    Usage: ZZ_pX g; FindPrimitivePoly(g, p, n);
*/
void FindPrimitivePoly(ZZ_pX &g, ZZ p, long n) {
  ZZ q2 = power(p, n);
  long q1;
  conv(q1, q2 - ZZ(1));

  vector<long> factors = FindFactor(q1);

  bool flag = 0;
  while (!flag) {
    ZZ_pX F;
    BuildIrred(F, n);

    ZZ_pX f;
    SetCoeff(f, 0, -1);
    flag = 1;
    for (size_t i = 0; i + 1 < factors.size(); ++i) {
      if (factors[i] <= n)
        continue;

      SetCoeff(f, factors[i], 1);
      ZZ_pX q, r;
      DivRem(q, r, f, F);
      if (r == 0) {
        flag = 0;
        break;
      }
    }
    if (flag == 1) {
      g = F;
    }
  }
  return;
}

/*
    Interpolates f such that f(a[i]) == b[i] over GR(p^l, s).
    - If l == 1: delegates to NTL::interpolate (field case).
    - If l > 1: uses a ring-aware incremental algorithm and Inv() for unit
   inverses. Preconditions: NTL contexts match modulus p^l and extension degree
   s. Usage: interpolate_for_GR(f, a, b, p, l, s);
*/
void interpolate_for_GR(ZZ_pEX &f, const vec_ZZ_pE &a, const vec_ZZ_pE &b, ZZ p,
                        long l, long s) {
  (void)p;
  if (l == 1) {
    interpolate(f, a, b);
    return;
  }
  long m = a.length();
  if (b.length() != m)
    LogicError("interpolate: vector length mismatch");

  if (m == 0) {
    clear(f);
    return;
  }
  vec_ZZ_pE prod;
  prod = a;
  ZZ_pE t1, t2;

  long k, i;

  vec_ZZ_pE res;
  res.SetLength(m);

  for (k = 0; k < m; k++) {
    const ZZ_pE &aa = a[k];

    set(t1);
    for (i = k - 1; i >= 0; i--) {
      mul(t1, t1, aa);
      add(t1, t1, prod[i]);
    }

    clear(t2);
    for (i = k - 1; i >= 0; i--) {
      mul(t2, t2, aa);
      add(t2, t2, res[i]);
    }
    t1 = Inv(t1, s);
    sub(t2, b[k], t2);
    mul(t1, t1, t2);

    for (i = 0; i < k; i++) {
      mul(t2, prod[i], t1);
      add(res[i], res[i], t2);
    }

    res[k] = t1;
    if (k < m - 1) {
      if (k == 0) {
        prod[0] = -prod[0];
      } else {
        t1 = -a[k];
        add(prod[k], t1, prod[k - 1]);
        for (i = k - 1; i >= 1; i--) {
          mul(t2, prod[i], t1);
          add(prod[i], t2, prod[i - 1]);
        }
        mul(prod[0], prod[0], t1);
      }
    }
  }

  while (m > 0 && IsZero(res[m - 1]))
    m--;
  res.SetLength(m);
  f.rep = res;
}

/*
    Splits input into fixed-size groups and pads the last group with zeros if
   needed. Usage: vector<vector<long>> groups = splitVector(v, groupSize);
*/
vector<vector<long>> splitVector(const vector<long> &input, int groupSize) {
  vector<vector<long>> result;
  int totalSize = input.size();
  int numGroups = (totalSize + groupSize - 1) / groupSize;
  result.resize(numGroups, vector<long>(groupSize, 0));

  for (int i = 0; i < numGroups; ++i) {
    vector<long> &group = result[static_cast<size_t>(i)];
    const int groupStart = i * groupSize;
    const int copyLen = min(groupSize, totalSize - groupStart);
    for (int j = 0; j < copyLen; ++j) {
      group[static_cast<size_t>(j)] = input[static_cast<size_t>(groupStart + j)];
    }
  }

  return result;
}
