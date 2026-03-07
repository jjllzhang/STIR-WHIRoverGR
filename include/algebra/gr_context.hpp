#ifndef SWGR_ALGEBRA_GR_CONTEXT_HPP_
#define SWGR_ALGEBRA_GR_CONTEXT_HPP_

#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/ZZ_pX.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
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
  std::vector<GRElem> batch_inv(std::span<const GRElem> xs) const;

  std::vector<std::uint8_t> serialize(const GRElem& x) const;
  GRElem deserialize(std::span<const std::uint8_t> bytes) const;

  template <typename Fn>
  decltype(auto) with_ntl_context(Fn&& fn) const {
    ensure_backend_initialized();
    NTL::ZZ_pPush mod_push(modulus_);
    NTL::ZZ_pEPush ext_push(lazy_state_->extension_polynomial);
    return std::forward<Fn>(fn)();
  }

  template <typename Fn>
  void parallel_for_with_ntl_context(std::ptrdiff_t count, bool parallelize,
                                     Fn&& fn) const {
    if (count <= 0) {
      return;
    }
#if defined(SWGR_HAS_OPENMP)
#pragma omp parallel if(parallelize)
    {
      with_ntl_context([&] {
#pragma omp for schedule(static)
        for (std::ptrdiff_t i = 0; i < count; ++i) {
          fn(i);
        }
      });
    }
#else
    (void)parallelize;
    with_ntl_context([&] {
      for (std::ptrdiff_t i = 0; i < count; ++i) {
        fn(i);
      }
    });
#endif
  }

  template <typename Fn>
  void parallel_for_chunks_with_ntl_context(std::ptrdiff_t count,
                                            std::ptrdiff_t chunk_size,
                                            bool parallelize, Fn&& fn) const {
    if (count <= 0) {
      return;
    }
    if (chunk_size <= 0) {
      throw std::invalid_argument("chunk_size must be > 0");
    }

    const std::ptrdiff_t chunk_count = (count + chunk_size - 1) / chunk_size;
#if defined(SWGR_HAS_OPENMP)
#pragma omp parallel if(parallelize)
    {
      with_ntl_context([&] {
#pragma omp for schedule(static)
        for (std::ptrdiff_t chunk_index = 0; chunk_index < chunk_count;
             ++chunk_index) {
          const std::ptrdiff_t begin = chunk_index * chunk_size;
          const std::ptrdiff_t end =
              begin + chunk_size < count ? begin + chunk_size : count;
          fn(begin, end);
        }
      });
    }
#else
    (void)parallelize;
    with_ntl_context([&] {
      for (std::ptrdiff_t chunk_index = 0; chunk_index < chunk_count;
           ++chunk_index) {
        const std::ptrdiff_t begin = chunk_index * chunk_size;
        const std::ptrdiff_t end =
            begin + chunk_size < count ? begin + chunk_size : count;
        fn(begin, end);
      }
    });
#endif
  }

 private:
  struct LazyState {
    std::once_flag backend_once;
    std::once_flag teich_once;
    NTL::ZZ_pX base_irreducible_mod_p;
    NTL::ZZ_pX extension_polynomial;
    GRElem teich_generator;
  };

  void initialize();
  void ensure_backend_initialized() const;
  void ensure_teich_generator_initialized() const;

  GRConfig cfg_;
  mutable NTL::ZZ prime_;
  mutable NTL::ZZ modulus_;
  mutable std::shared_ptr<LazyState> lazy_state_;
};

}  // namespace swgr::algebra

#endif  // SWGR_ALGEBRA_GR_CONTEXT_HPP_
