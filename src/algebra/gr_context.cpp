#include "algebra/gr_context.hpp"

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pX.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "GaloisRing/Inverse.hpp"
#include "GaloisRing/PrimitiveElement.hpp"
#include "GaloisRing/utils.hpp"

using NTL::BytesFromZZ;
using NTL::NumBits;
using NTL::ProbPrime;
using NTL::RandomStreamPush;
using NTL::SetCoeff;
using NTL::SetSeed;
using NTL::ZZ;
using NTL::ZZFromBytes;
using NTL::ZZ_p;
using NTL::ZZ_pPush;
using NTL::ZZ_pE;
using NTL::ZZ_pEPush;
using NTL::ZZ_pX;
using NTL::coeff;
using NTL::conv;
using NTL::power;
using NTL::rep;
using NTL::set;
using NTL::to_ZZ;

namespace swgr::algebra {
namespace {

long CheckedLong(std::uint64_t value, const char* label) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

bool IsUnitByReductionModP(const ZZ_pE& x, const ZZ& p, long r) {
  if (x == 0) {
    return false;
  }

  const ZZ_pX poly = rep(x);
  for (long i = 0; i < r; ++i) {
    ZZ c = rep(coeff(poly, i));
    c %= p;
    if (c < 0) {
      c += p;
    }
    if (c != 0) {
      return true;
    }
  }
  return false;
}

std::vector<long> ExportPolynomialCoefficients(const ZZ_pX& poly) {
  std::vector<long> coefficients;
  ZZpX2long(poly, coefficients);
  return coefficients;
}

ZZ DeterministicPrimitivePolySeed(const GRConfig& cfg) {
  std::vector<unsigned char> bytes;
  bytes.reserve(sizeof(std::uint64_t) * 4);
  const std::uint64_t words[] = {
      0x5357475247524354ULL,
      cfg.p,
      cfg.k_exp,
      cfg.r,
  };
  for (const auto word : words) {
    for (std::size_t i = 0; i < sizeof(word); ++i) {
      bytes.push_back(
          static_cast<unsigned char>((word >> (8U * i)) & 0xFFU));
    }
  }
  return ZZFromBytes(bytes.data(), static_cast<long>(bytes.size()));
}

}  // namespace

GRContext::GRContext(const GRConfig& cfg) : cfg_(cfg) { initialize(); }

const ZZ_pX& GRContext::base_irreducible_mod_p() const {
  ensure_backend_initialized();
  return base_irreducible_mod_p_;
}

const ZZ_pX& GRContext::extension_polynomial() const {
  ensure_backend_initialized();
  return extension_polynomial_;
}

std::size_t GRContext::coeff_bytes() const {
  const ZZ max_coeff = modulus_ - 1;
  return static_cast<std::size_t>((NTL::NumBits(max_coeff) + 7) / 8);
}

std::size_t GRContext::elem_bytes() const {
  return coeff_bytes() * static_cast<std::size_t>(cfg_.r);
}

GRElem GRContext::zero() const { return with_ntl_context([] { return GRElem(0); }); }

GRElem GRContext::one() const {
  return with_ntl_context([] {
    GRElem one;
    set(one);
    return one;
  });
}

GRElem GRContext::teich_generator() const {
  ensure_teich_generator_initialized();
  return teich_generator_;
}

bool GRContext::is_unit(const GRElem& x) const {
  return with_ntl_context([&] {
    return IsUnitByReductionModP(x, prime_, CheckedLong(cfg_.r, "r"));
  });
}

GRElem GRContext::inv(const GRElem& x) const {
  if (!is_unit(x)) {
    throw std::invalid_argument("GRContext::inv requires a unit");
  }

  return with_ntl_context([&] {
    const GRElem result = Inv(x, CheckedLong(cfg_.r, "r"));
    if (result == 0) {
      throw std::runtime_error("GRContext::inv failed to invert unit");
    }
    return result;
  });
}

std::vector<std::uint8_t> GRContext::serialize(const GRElem& x) const {
  return with_ntl_context([&] {
    const std::size_t width = coeff_bytes();
    const long r = CheckedLong(cfg_.r, "r");
    std::vector<std::uint8_t> out(elem_bytes(), 0);
    const ZZ_pX poly = rep(x);

    for (long i = 0; i < r; ++i) {
      ZZ value = rep(coeff(poly, i));
      value %= modulus_;
      if (value < 0) {
        value += modulus_;
      }
      BytesFromZZ(reinterpret_cast<unsigned char*>(out.data()) +
                      static_cast<std::size_t>(i) * width,
                  value, static_cast<long>(width));
    }
    return out;
  });
}

GRElem GRContext::deserialize(std::span<const std::uint8_t> bytes) const {
  if (bytes.size() != elem_bytes()) {
    throw std::invalid_argument("GRContext::deserialize size mismatch");
  }

  return with_ntl_context([&] {
    const std::size_t width = coeff_bytes();
    const long r = CheckedLong(cfg_.r, "r");
    ZZ_pX poly;

    for (long i = 0; i < r; ++i) {
      ZZ value;
      ZZFromBytes(
          value,
          reinterpret_cast<const unsigned char*>(bytes.data()) +
              static_cast<std::size_t>(i) * width,
          static_cast<long>(width));
      value %= modulus_;
      if (value < 0) {
        value += modulus_;
      }
      if (value != 0) {
        SetCoeff(poly, i, conv<ZZ_p>(value));
      }
    }

    GRElem result;
    conv(result, poly);
    return result;
  });
}

void GRContext::initialize() {
  if (cfg_.p <= 1) {
    throw std::invalid_argument("GRContext requires p > 1");
  }
  if (cfg_.k_exp == 0) {
    throw std::invalid_argument("GRContext requires k_exp > 0");
  }
  if (cfg_.r == 0) {
    throw std::invalid_argument("GRContext requires r > 0");
  }

  prime_ = to_ZZ(cfg_.p);
  if (!ProbPrime(prime_)) {
    throw std::invalid_argument("GRContext currently expects prime p");
  }

  const long k_long = CheckedLong(cfg_.k_exp, "k_exp");
  modulus_ = power(prime_, k_long);
}

void GRContext::ensure_backend_initialized() const {
  if (backend_initialized_) {
    return;
  }

  const long r_long = CheckedLong(cfg_.r, "r");
  {
    ZZ_pPush mod_p_push(prime_);
    RandomStreamPush seed_push;
    SetSeed(DeterministicPrimitivePolySeed(cfg_));
    FindPrimitivePoly(base_irreducible_mod_p_, prime_, r_long);
  }

  const std::vector<long> lifted_coefficients =
      ExportPolynomialCoefficients(base_irreducible_mod_p_);

  {
    ZZ_pPush modulus_push(modulus_);
    extension_polynomial_ = long2ZZpX(lifted_coefficients);
  }

  backend_initialized_ = true;
}

void GRContext::ensure_teich_generator_initialized() const {
  if (teich_generator_initialized_) {
    return;
  }

  ensure_backend_initialized();

  const long k_long = CheckedLong(cfg_.k_exp, "k_exp");
  const long r_long = CheckedLong(cfg_.r, "r");
  const long max_long_bits = static_cast<long>(8 * sizeof(long) - 1);
  const ZZ teich_order = power(prime_, r_long) - ZZ(1);

  if (NumBits(teich_order) <= max_long_bits) {
    teich_generator_ =
        FindTeichmullerGenerator(prime_, k_long, r_long, extension_polynomial_);
  } else {
    ZZ_pPush modulus_push(modulus_);
    ZZ_pEPush ext_push(extension_polynomial_);
    ZZ_pX x_poly;
    SetCoeff(x_poly, 1, 1);
    ZZ_pE x;
    conv(x, x_poly);
    teich_generator_ = power(x, power(prime_, k_long - 1));
  }

  teich_generator_initialized_ = true;
}

}  // namespace swgr::algebra
