#ifndef GALOISRING_UTILS_HPP_
#define GALOISRING_UTILS_HPP_

#include <NTL/ZZ.h>
#include <NTL/ZZX.h>
#include <NTL/ZZXFactoring.h>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pEX.h>
#include <NTL/ZZ_pEXFactoring.h>
#include <NTL/ZZ_pXFactoring.h>
#include <NTL/mat_ZZ.h>
#include <NTL/mat_ZZ_pE.h>
#include <NTL/vec_ZZ.h>
#include <NTL/vec_ZZ_p.h>
#include <NTL/vec_ZZ_pE.h>
#include <NTL/vec_vec_ZZ_pE.h>
#include <NTL/vector.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "GaloisRing/Inverse.hpp"

// NOTE ABOUT NTL CONTEXTS
// NTL types such as ZZ_p/ZZ_pX/ZZ_pE/ZZ_pEX depend on global "current modulus"
// contexts. Before calling conversion/interpolation helpers below, make sure
// you have initialized:
//   - ZZ_p::init(modulus)   (e.g., p or p^k)
//   - ZZ_pE::init(F)        (an irreducible/primitive polynomial of degree s,
//   when ZZ_pE/ZZ_pEX are involved)

// Returns all positive divisors of n in ascending order.
// Call: vector<long> factors = FindFactor(8);  // {1,2,4,8}
std::vector<long> FindFactor(long n);

// Converts coefficients (low degree -> high degree) into an NTL polynomial over
// ZZ_p. Precondition: ZZ_p::init(modulus) has been called. Call: ZZ_pX f =
// long2ZZpX({1,2,3});  // 1 + 2*x + 3*x^2
NTL::ZZ_pX long2ZZpX(const std::vector<long> &coefficients);

// Converts coefficients into a ZZ_pE element by reducing a ZZ_pX representation
// modulo the current ZZ_pE modulus. Preconditions: ZZ_p::init(modulus) and
// ZZ_pE::init(F) have been called. Call: ZZ_pE a = long2ZZpE({3,5});  // 3 +
// 5*x in the current ZZ_pE
NTL::ZZ_pE long2ZZpE(const std::vector<long> &coefficients);

// Returns true iff n is a power of two (and n > 0).
bool isPowerOfTwo(long n);

// Flattens a vec_ZZ_pE into vector<long> by expanding each element into s
// coefficients (basis 1..x^(s-1)). Call: VeczzpE2Veclong(v, out, s);
void VeczzpE2Veclong(const NTL::vec_ZZ_pE &v1, std::vector<long> &m, long s);

// Concatenates the decimal representations of the entries without separators.
// Call: string s = Veclong2String({1,0,23});  // "1023"
std::string Veclong2String(const std::vector<long> &vec);

// Expands one ZZ_pE element into vector<long> of length s (coefficients of
// rep(a) for degrees 0..s-1). Call: ZzpE2Veclong(a, out, s);
void ZzpE2Veclong(const NTL::ZZ_pE &F, std::vector<long> &m, long s);

// Same as VeczzpE2Veclong, but each element is converted to a concatenated
// string (see Veclong2String). Call: VeczzpE2Vecstring(v, out, s);
void VeczzpE2Vecstring(const NTL::vec_ZZ_pE &v1, std::vector<std::string> &m,
                       long s);

// Extracts coefficients of a ZZ_pX into vector<long> (degrees 0..deg(F)).
// Call: ZZpX2long(poly, out);
void ZZpX2long(const NTL::ZZ_pX &F, std::vector<long> &Irred);

// Flattens a ZZ_pEX (polynomial with ZZ_pE coefficients) into vector<long>,
// expanding each coefficient into s longs. Call: ZZpEX2long(f, out, s);
void ZZpEX2long(const NTL::ZZ_pEX &v1, std::vector<long> &m, long s);

// Returns true if all elements in vec are non-zero.
bool allNonZero(const NTL::vec_ZZ_pE &vec);

// Returns the smallest power of two >= n.
// Precondition: n >= 0 (negative inputs are not supported).
long nextPowerOf2(long n);

// Prints a vector<long> in a simple "[a b c ]" format to stdout.
void print(std::vector<long> &vec);

// Pads (or truncates) input to targetLength by appending zeros.
// Call: auto padded = PadVectorToLength(v, 16);
std::vector<long> PadVectorToLength(const std::vector<long> &input,
                                    std::size_t targetLength);

// Removes trailing zeros. Returns an empty vector if all entries are zero.
// Call: auto trimmed = TrimVector({1,2,0,0});  // {1,2}
std::vector<long> TrimVector(const std::vector<long> &input);

// Splits input into numSegments equal-size chunks (using floor division), pads
// each chunk to segmentLength, and concatenates. Note: if input.size() is not
// divisible by numSegments, trailing elements may be ignored. Call: auto out =
// SplitAndPadVector(v, segmentLength, numSegments);
std::vector<long> SplitAndPadVector(const std::vector<long> &input,
                                    long segmentLength, long numSegments);

// Converts a flattened vector<long> into a ZZ_pEX by grouping every s entries
// as one ZZ_pE coefficient. Preconditions: ZZ_p/ZZ_pE contexts are initialized;
// result.size() must be a multiple of s. Call: Long2ZZpEX(flat, poly, s);
void Long2ZZpEX(const std::vector<long> &result, NTL::ZZ_pEX &V, long s);

// Same as Long2ZZpEX, but explicitly specifies the number of ZZ_pE coefficients
// (n). Preconditions: result.size() == n*s.
void Long2ZZpEX2(const std::vector<long> &result, NTL::ZZ_pEX &V, long s,
                 long n);

// Returns the smallest perfect square strictly greater than num.
// Precondition: num >= 0.
int nearestPerfectSquare(int num);

// Serializes an irreducible polynomial F (ZZ_pX) into vector<long>
// coefficients. Call: fillIrred(F, out);
void fillIrred(const NTL::ZZ_pX &F, std::vector<long> &Irred);

// Serializes interpolation points a[i] (ZZ_pE) into vector<long>, expanding
// each point into s coefficients. Call: fillInterpolation(points, out, s);
void fillInterpolation(const NTL::vec_ZZ_pE &v1,
                       std::vector<long> &Interpolation, long s);

// Attempts to find a degree-n polynomial in Z_p[x] intended to be "primitive"
// (generator polynomial). Precondition: ZZ_p::init(p) has been called. Call:
// ZZ_pX g; FindPrimitivePoly(g, p, n);
void FindPrimitivePoly(NTL::ZZ_pX &g, NTL::ZZ p, long n);

// Interpolates f such that f(a[i]) == b[i] over GR(p^l, s).
// - If l == 1, delegates to NTL::interpolate (field case).
// - If l > 1, uses a ring-aware incremental algorithm and Inv() for required
// unit inverses. Preconditions: NTL contexts match modulus p^l and extension
// degree s; required denominators must be units. Call: interpolate_for_GR(f, a,
// b, p, l, s);
void interpolate_for_GR(NTL::ZZ_pEX &f, const NTL::vec_ZZ_pE &a,
                        const NTL::vec_ZZ_pE &b, NTL::ZZ p, long l, long s);

// Splits input into groups of fixed size, padding the last group with zeros.
// Call: auto groups = splitVector(v, groupSize);
std::vector<std::vector<long>> splitVector(const std::vector<long> &input,
                                           int groupSize);
// void interpolate_for_GR_generate_cache(ZZ_pEX& f, const vec_ZZ_pE& a, const
// vec_ZZ_pE& b, ZZ p, long l, long s); void interpolate_for_GR_cache(ZZ_pEX& f,
// const vec_ZZ_pE& a, const vec_ZZ_pE& b, ZZ p, long l, long s);
#endif  // GALOISRING_UTILS_HPP_
