#ifndef SWGR_ALGEBRA_GR_CONTEXT_HPP_
#define SWGR_ALGEBRA_GR_CONTEXT_HPP_

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pX.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace swgr::algebra {

using GRElem = NTL::ZZ_pE;

struct GRConfig {
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 54;
};

class GRContext {
 public:
  explicit GRContext(const GRConfig& cfg);

  const GRConfig& config() const { return cfg_; }
  const NTL::ZZ& prime() const { return prime_; }
  const NTL::ZZ& modulus() const { return modulus_; }
  const NTL::ZZ_pX& base_irreducible_mod_p() const;
  const NTL::ZZ_pX& extension_polynomial() const;

  std::size_t coeff_bytes() const;
  std::size_t elem_bytes() const;

  GRElem zero() const;
  GRElem one() const;
  GRElem teich_generator() const;

  bool is_unit(const GRElem& x) const;
  GRElem inv(const GRElem& x) const;

  std::vector<std::uint8_t> serialize(const GRElem& x) const;
  GRElem deserialize(std::span<const std::uint8_t> bytes) const;

  template <typename Fn>
  decltype(auto) with_ntl_context(Fn&& fn) const {
    ensure_backend_initialized();
    NTL::ZZ_pPush mod_push(modulus_);
    NTL::ZZ_pEPush ext_push(extension_polynomial_);
    return std::forward<Fn>(fn)();
  }

 private:
  void initialize();
  void ensure_backend_initialized() const;
  void ensure_teich_generator_initialized() const;

  GRConfig cfg_;
  mutable NTL::ZZ prime_;
  mutable NTL::ZZ modulus_;
  mutable NTL::ZZ_pX base_irreducible_mod_p_;
  mutable NTL::ZZ_pX extension_polynomial_;
  mutable GRElem teich_generator_;
  mutable bool backend_initialized_ = false;
  mutable bool teich_generator_initialized_ = false;
};

}  // namespace swgr::algebra

#endif  // SWGR_ALGEBRA_GR_CONTEXT_HPP_
