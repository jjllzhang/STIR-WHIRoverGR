#include "poly_utils/fft3.hpp"

#include <NTL/ZZ_pE.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(STIR_WHIR_GR_HAS_OPENMP)
#include <omp.h>
#endif

using NTL::clear;
using NTL::power;

namespace stir_whir_gr::poly_utils {
namespace {

constexpr std::uint64_t kParallelRadix3Threshold = 27U;

bool IsPowerOfThree(std::uint64_t value) {
  if (value == 0) {
    return false;
  }
  while (value % 3 == 0) {
    value /= 3;
  }
  return value == 1;
}

long CheckedLong(std::uint64_t value, const char* label) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

std::ptrdiff_t CheckedPtrdiff(std::uint64_t value, const char* label) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds ptrdiff_t");
  }
  return static_cast<std::ptrdiff_t>(value);
}

algebra::GRElem ConstantFromUint64(const algebra::GRContext& ctx,
                                   std::uint64_t value) {
  algebra::GRElem out;
  clear(out);
  auto addend = ctx.one();
  while (value > 0) {
    if ((value & 1U) != 0U) {
      out += addend;
    }
    value >>= 1U;
    if (value != 0) {
      addend += addend;
    }
  }
  return out;
}

algebra::GRElem EvaluateCoefficientsAt(
    std::span<const algebra::GRElem> coefficients, const algebra::GRElem& x) {
  algebra::GRElem acc;
  clear(acc);
  for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
    acc *= x;
    acc += *it;
  }
  return acc;
}

std::uint64_t ResidueChildSize(std::uint64_t size, std::size_t residue) {
  if (size <= residue) {
    return 0;
  }
  return 1U + ((size - static_cast<std::uint64_t>(residue) - 1U) / 3U);
}

struct StridedConstGRElemView {
  const algebra::GRElem* data = nullptr;
  std::uint64_t size = 0;
  std::uint64_t stride = 1;

  const algebra::GRElem& operator[](std::uint64_t index) const {
    return data[static_cast<std::size_t>(index * stride)];
  }

  StridedConstGRElemView Subview(std::size_t residue) const {
    const std::uint64_t child_size = ResidueChildSize(size, residue);
    if (child_size == 0) {
      return {.data = data, .size = 0, .stride = stride * 3U};
    }
    return {
        .data = data + static_cast<std::size_t>(residue) * stride,
        .size = child_size,
        .stride = stride * 3U,
    };
  }
};

struct StridedMutableGRElemView {
  algebra::GRElem* data = nullptr;
  std::uint64_t size = 0;
  std::uint64_t stride = 1;

  algebra::GRElem& operator[](std::uint64_t index) const {
    return data[static_cast<std::size_t>(index * stride)];
  }

  StridedMutableGRElemView Subview(std::size_t residue) const {
    const std::uint64_t child_size = ResidueChildSize(size, residue);
    if (child_size == 0) {
      return {.data = data, .size = 0, .stride = stride * 3U};
    }
    return {
        .data = data + static_cast<std::size_t>(residue) * stride,
        .size = child_size,
        .stride = stride * 3U,
    };
  }
};

std::span<const algebra::GRElem> MakeConstSpan(
    const StridedConstGRElemView& view,
    std::vector<algebra::GRElem>* scratch) {
  if (view.size == 0) {
    scratch->clear();
    return std::span<const algebra::GRElem>(*scratch);
  }
  if (view.stride == 1U) {
    return std::span<const algebra::GRElem>(view.data,
                                            static_cast<std::size_t>(view.size));
  }

  scratch->resize(static_cast<std::size_t>(view.size));
  for (std::uint64_t index = 0; index < view.size; ++index) {
    (*scratch)[static_cast<std::size_t>(index)] = view[index];
  }
  return std::span<const algebra::GRElem>(*scratch);
}

struct Radix3Stage {
  std::uint64_t size = 1;
  std::uint64_t reduced_size = 0;
  algebra::GRElem offset;
  algebra::GRElem offset_inv;
  algebra::GRElem root;
  algebra::GRElem root_inv;
  algebra::GRElem zeta;
  algebra::GRElem zeta_sq;
};

struct Radix3Plan {
  std::shared_ptr<const algebra::GRContext> ctx;
  algebra::GRElem inv_three;
  std::vector<Radix3Stage> stages;
};

struct Radix3PlanCacheKey {
  const algebra::GRContext* ctx = nullptr;
  std::uint64_t size = 0;
  std::vector<std::uint8_t> offset_bytes;
  std::vector<std::uint8_t> root_bytes;

  bool operator==(const Radix3PlanCacheKey& other) const {
    return ctx == other.ctx && size == other.size &&
           offset_bytes == other.offset_bytes &&
           root_bytes == other.root_bytes;
  }
};

std::size_t CombineHash(std::size_t seed, std::size_t value) {
  constexpr std::size_t kHashMix = 0x9e3779b97f4a7c15ULL;
  return seed ^ (value + kHashMix + (seed << 6U) + (seed >> 2U));
}

std::size_t HashBytes(const std::vector<std::uint8_t>& bytes) {
  std::size_t seed = bytes.size();
  for (const auto byte : bytes) {
    seed = CombineHash(seed, static_cast<std::size_t>(byte));
  }
  return seed;
}

struct Radix3PlanCacheKeyHash {
  std::size_t operator()(const Radix3PlanCacheKey& key) const {
    std::size_t seed = 0;
    seed = CombineHash(
        seed, std::hash<const algebra::GRContext*>{}(key.ctx));
    seed = CombineHash(seed, std::hash<std::uint64_t>{}(key.size));
    seed = CombineHash(seed, HashBytes(key.offset_bytes));
    seed = CombineHash(seed, HashBytes(key.root_bytes));
    return seed;
  }
};

struct Radix3PlanCacheState {
  std::shared_mutex mutex;
  std::unordered_map<Radix3PlanCacheKey, std::shared_ptr<const Radix3Plan>,
                     Radix3PlanCacheKeyHash>
      entries;
};

Radix3PlanCacheState& GlobalRadix3PlanCache() {
  static Radix3PlanCacheState cache;
  return cache;
}

Radix3PlanCacheKey BuildRadix3PlanCacheKey(const Domain& domain) {
  Radix3PlanCacheKey key;
  key.ctx = domain.context_ptr().get();
  key.size = domain.size();
  key.offset_bytes = domain.context().serialize(domain.offset());
  key.root_bytes = domain.context().serialize(domain.root());
  return key;
}

Radix3Plan BuildRadix3Plan(const Domain& domain) {
  Radix3Plan plan;
  plan.ctx = domain.context_ptr();
  const auto& ctx = domain.context();
  plan.inv_three = ctx.inv(ConstantFromUint64(ctx, 3));

  std::uint64_t current_size = domain.size();
  algebra::GRElem current_offset = domain.offset();
  algebra::GRElem current_root = domain.root();

  while (true) {
    Radix3Stage stage;
    stage.size = current_size;
    stage.offset = current_offset;

    if (current_size > 1) {
      stage.reduced_size = current_size / 3;
      stage.offset_inv = ctx.inv(current_offset);
      stage.root = current_root;
      stage.root_inv = ctx.inv(current_root);
      stage.zeta =
          power(current_root, CheckedLong(stage.reduced_size, "reduced_size"));
      stage.zeta_sq = stage.zeta * stage.zeta;
    }

    plan.stages.push_back(std::move(stage));
    if (current_size == 1) {
      break;
    }

    current_size /= 3;
    current_offset = power(current_offset, 3);
    current_root = power(current_root, 3);
  }

  return plan;
}

std::shared_ptr<const Radix3Plan> GetOrBuildRadix3Plan(const Domain& domain) {
  Radix3PlanCacheKey key = BuildRadix3PlanCacheKey(domain);
  auto& cache = GlobalRadix3PlanCache();

  {
    std::shared_lock<std::shared_mutex> read_lock(cache.mutex);
    const auto it = cache.entries.find(key);
    if (it != cache.entries.end()) {
      return it->second;
    }
  }

  auto plan = std::make_shared<const Radix3Plan>(BuildRadix3Plan(domain));

  std::unique_lock<std::shared_mutex> write_lock(cache.mutex);
  const auto [it, inserted] = cache.entries.emplace(std::move(key), plan);
  if (!inserted) {
    return it->second;
  }
  return plan;
}

int CurrentMaxThreads() {
#if defined(STIR_WHIR_GR_HAS_OPENMP)
  return omp_get_max_threads();
#else
  return 1;
#endif
}

bool ShouldParallelizeRadix3Stage(std::uint64_t reduced_size) {
  return reduced_size >= kParallelRadix3Threshold && CurrentMaxThreads() > 1;
}

std::ptrdiff_t ChooseRadix3ChunkSize(std::uint64_t reduced_size,
                                     int max_threads) {
  std::uint64_t chunk_size = 8U;
  if (max_threads > 0) {
    const std::uint64_t target_chunks =
        static_cast<std::uint64_t>(max_threads) * 4U;
    const std::uint64_t dynamic_chunk =
        (reduced_size + target_chunks - 1U) / target_chunks;
    chunk_size = std::max(chunk_size, dynamic_chunk);
  }
  return CheckedPtrdiff(chunk_size, "radix3 chunk_size");
}

algebra::GRElem StageStart(const algebra::GRElem& offset,
                           const algebra::GRElem& root,
                           std::ptrdiff_t begin,
                           const char* label) {
  return offset * power(root, CheckedLong(static_cast<std::uint64_t>(begin), label));
}

void ForwardCombineRadix3StageSerial(const Radix3Stage& stage,
                                     std::span<const algebra::GRElem> evals_mod0,
                                     std::span<const algebra::GRElem> evals_mod1,
                                     std::span<const algebra::GRElem> evals_mod2,
                                     std::span<algebra::GRElem> out) {
  algebra::GRElem x = stage.offset;
  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  for (std::uint64_t base = 0; base < stage.reduced_size; ++base) {
    const std::size_t index = static_cast<std::size_t>(base);
    const algebra::GRElem& eval0 = evals_mod0[index];
    const algebra::GRElem& eval1 = evals_mod1[index];
    const algebra::GRElem& eval2 = evals_mod2[index];

    const algebra::GRElem x_sq = x * x;
    const algebra::GRElem x_zeta = x * stage.zeta;
    const algebra::GRElem x_zeta_sq = x * stage.zeta_sq;

    out[index] = eval0 + x * eval1 + x_sq * eval2;
    out[index + reduced_size] =
        eval0 + x_zeta * eval1 + (x_sq * stage.zeta_sq) * eval2;
    out[index + 2U * reduced_size] =
        eval0 + x_zeta_sq * eval1 + (x_sq * stage.zeta) * eval2;

    x *= stage.root;
  }
}

void ForwardCombineRadix3Stage(const Radix3Plan& plan, const Radix3Stage& stage,
                               std::span<const algebra::GRElem> evals_mod0,
                               std::span<const algebra::GRElem> evals_mod1,
                               std::span<const algebra::GRElem> evals_mod2,
                               std::span<algebra::GRElem> out) {
  if (!ShouldParallelizeRadix3Stage(stage.reduced_size)) {
    ForwardCombineRadix3StageSerial(stage, evals_mod0, evals_mod1, evals_mod2,
                                    out);
    return;
  }

  const int max_threads = CurrentMaxThreads();
  const std::ptrdiff_t reduced_count =
      CheckedPtrdiff(stage.reduced_size, "radix3 reduced_size");
  const std::ptrdiff_t chunk_size =
      ChooseRadix3ChunkSize(stage.reduced_size, max_threads);
  if (reduced_count <= chunk_size) {
    ForwardCombineRadix3StageSerial(stage, evals_mod0, evals_mod1, evals_mod2,
                                    out);
    return;
  }

  plan.ctx->parallel_for_chunks_with_ntl_context(
      reduced_count, chunk_size, true,
      [&](std::ptrdiff_t begin, std::ptrdiff_t end) {
        algebra::GRElem x =
            StageStart(stage.offset, stage.root, begin, "forward chunk begin");
        const std::size_t reduced_size =
            static_cast<std::size_t>(stage.reduced_size);
        for (std::ptrdiff_t base = begin; base < end; ++base) {
          const std::size_t index = static_cast<std::size_t>(base);
          const algebra::GRElem& eval0 = evals_mod0[index];
          const algebra::GRElem& eval1 = evals_mod1[index];
          const algebra::GRElem& eval2 = evals_mod2[index];

          const algebra::GRElem x_sq = x * x;
          const algebra::GRElem x_zeta = x * stage.zeta;
          const algebra::GRElem x_zeta_sq = x * stage.zeta_sq;

          out[index] = eval0 + x * eval1 + x_sq * eval2;
          out[index + reduced_size] =
              eval0 + x_zeta * eval1 + (x_sq * stage.zeta_sq) * eval2;
          out[index + 2U * reduced_size] =
              eval0 + x_zeta_sq * eval1 + (x_sq * stage.zeta) * eval2;

          x *= stage.root;
        }
      });
}

void InverseDecodeRadix3StageSerial(const Radix3Plan& plan,
                                    const Radix3Stage& stage,
                                    std::span<const algebra::GRElem> evals,
                                    std::span<algebra::GRElem> decoded_mod0,
                                    std::span<algebra::GRElem> decoded_mod1,
                                    std::span<algebra::GRElem> decoded_mod2) {
  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  algebra::GRElem x_inv = stage.offset_inv;
  for (std::uint64_t base = 0; base < stage.reduced_size; ++base) {
    const std::size_t index = static_cast<std::size_t>(base);
    const auto& value0 = evals[index];
    const auto& value1 = evals[index + reduced_size];
    const auto& value2 = evals[index + 2U * reduced_size];

    const algebra::GRElem b0 = (value0 + value1 + value2) * plan.inv_three;
    const algebra::GRElem b1 =
        (value0 + stage.zeta_sq * value1 + stage.zeta * value2) *
        plan.inv_three;
    const algebra::GRElem b2 =
        (value0 + stage.zeta * value1 + stage.zeta_sq * value2) *
        plan.inv_three;
    const algebra::GRElem x_sq_inv = x_inv * x_inv;

    decoded_mod0[index] = b0;
    decoded_mod1[index] = b1 * x_inv;
    decoded_mod2[index] = b2 * x_sq_inv;

    x_inv *= stage.root_inv;
  }
}

void InverseDecodeRadix3Stage(const Radix3Plan& plan, const Radix3Stage& stage,
                              std::span<const algebra::GRElem> evals,
                              std::span<algebra::GRElem> decoded_mod0,
                              std::span<algebra::GRElem> decoded_mod1,
                              std::span<algebra::GRElem> decoded_mod2) {
  if (!ShouldParallelizeRadix3Stage(stage.reduced_size)) {
    InverseDecodeRadix3StageSerial(plan, stage, evals, decoded_mod0,
                                   decoded_mod1, decoded_mod2);
    return;
  }

  const int max_threads = CurrentMaxThreads();
  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  const std::ptrdiff_t reduced_count =
      CheckedPtrdiff(stage.reduced_size, "radix3 reduced_size");
  const std::ptrdiff_t chunk_size =
      ChooseRadix3ChunkSize(stage.reduced_size, max_threads);
  if (reduced_count <= chunk_size) {
    InverseDecodeRadix3StageSerial(plan, stage, evals, decoded_mod0,
                                   decoded_mod1, decoded_mod2);
    return;
  }

  plan.ctx->parallel_for_chunks_with_ntl_context(
      reduced_count, chunk_size, true,
      [&](std::ptrdiff_t begin, std::ptrdiff_t end) {
        algebra::GRElem x_inv = StageStart(stage.offset_inv, stage.root_inv,
                                           begin, "inverse chunk begin");
        for (std::ptrdiff_t base = begin; base < end; ++base) {
          const std::size_t index = static_cast<std::size_t>(base);
          const auto& value0 = evals[index];
          const auto& value1 = evals[index + reduced_size];
          const auto& value2 = evals[index + 2U * reduced_size];

          const algebra::GRElem b0 =
              (value0 + value1 + value2) * plan.inv_three;
          const algebra::GRElem b1 =
              (value0 + stage.zeta_sq * value1 + stage.zeta * value2) *
              plan.inv_three;
          const algebra::GRElem b2 =
              (value0 + stage.zeta * value1 + stage.zeta_sq * value2) *
              plan.inv_three;
          const algebra::GRElem x_sq_inv = x_inv * x_inv;

          decoded_mod0[index] = b0;
          decoded_mod1[index] = b1 * x_inv;
          decoded_mod2[index] = b2 * x_sq_inv;

          x_inv *= stage.root_inv;
        }
      });
}

void ForwardRadix3Write(const Radix3Plan& plan, std::size_t level,
                        const StridedConstGRElemView& coefficients,
                        std::span<algebra::GRElem> output) {
  const auto& stage = plan.stages[level];
  if (stage.size == 1) {
    std::vector<algebra::GRElem> contiguous_coefficients;
    output[0] = EvaluateCoefficientsAt(
        MakeConstSpan(coefficients, &contiguous_coefficients), stage.offset);
    return;
  }

  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  std::vector<algebra::GRElem> child_evals(3U * reduced_size);
  auto evals_mod0 =
      std::span<algebra::GRElem>(child_evals.data(), reduced_size);
  auto evals_mod1 = std::span<algebra::GRElem>(child_evals.data() + reduced_size,
                                               reduced_size);
  auto evals_mod2 =
      std::span<algebra::GRElem>(child_evals.data() + 2U * reduced_size,
                                 reduced_size);

  ForwardRadix3Write(plan, level + 1U, coefficients.Subview(0), evals_mod0);
  ForwardRadix3Write(plan, level + 1U, coefficients.Subview(1), evals_mod1);
  ForwardRadix3Write(plan, level + 1U, coefficients.Subview(2), evals_mod2);

  ForwardCombineRadix3Stage(plan, stage, evals_mod0, evals_mod1, evals_mod2,
                            output);
}

void InverseRadix3Write(const Radix3Plan& plan, std::size_t level,
                        std::span<const algebra::GRElem> evals,
                        const StridedMutableGRElemView& coefficients) {
  const auto& stage = plan.stages[level];
  if (stage.size == 1) {
    coefficients[0] = evals[0];
    return;
  }

  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  std::vector<algebra::GRElem> decoded_values(3U * reduced_size);
  auto decoded_mod0 =
      std::span<algebra::GRElem>(decoded_values.data(), reduced_size);
  auto decoded_mod1 =
      std::span<algebra::GRElem>(decoded_values.data() + reduced_size,
                                 reduced_size);
  auto decoded_mod2 =
      std::span<algebra::GRElem>(decoded_values.data() + 2U * reduced_size,
                                 reduced_size);
  InverseDecodeRadix3Stage(plan, stage, evals, decoded_mod0, decoded_mod1,
                           decoded_mod2);

  InverseRadix3Write(plan, level + 1U, decoded_mod0, coefficients.Subview(0));
  InverseRadix3Write(plan, level + 1U, decoded_mod1, coefficients.Subview(1));
  InverseRadix3Write(plan, level + 1U, decoded_mod2, coefficients.Subview(2));
}

}  // namespace

std::vector<algebra::GRElem> fft3(const Domain& domain,
                                  const Polynomial& poly) {
  if (!IsPowerOfThree(domain.size())) {
    throw std::invalid_argument("poly_utils::fft3 requires a 3-smooth domain");
  }

  return domain.context().with_ntl_context([&] {
    const auto plan = GetOrBuildRadix3Plan(domain);
    std::vector<algebra::GRElem> evals(static_cast<std::size_t>(domain.size()));
    const StridedConstGRElemView coefficients{
        .data = poly.coefficients().data(),
        .size = static_cast<std::uint64_t>(poly.coefficients().size()),
        .stride = 1,
    };
    ForwardRadix3Write(*plan, 0, coefficients, evals);
    return evals;
  });
}

std::vector<algebra::GRElem> inverse_fft3(
    const Domain& domain, const std::vector<algebra::GRElem>& evals) {
  if (!IsPowerOfThree(domain.size())) {
    throw std::invalid_argument(
        "poly_utils::inverse_fft3 requires a 3-smooth domain");
  }
  if (evals.size() != domain.size()) {
    throw std::invalid_argument(
        "poly_utils::inverse_fft3 requires eval count == domain size");
  }

  return domain.context().with_ntl_context([&] {
    const auto plan = GetOrBuildRadix3Plan(domain);
    std::vector<algebra::GRElem> coefficients(
        static_cast<std::size_t>(domain.size()));
    const StridedMutableGRElemView coefficients_view{
        .data = coefficients.data(),
        .size = domain.size(),
        .stride = 1,
    };
    InverseRadix3Write(*plan, 0, evals, coefficients_view);
    return coefficients;
  });
}

}  // namespace stir_whir_gr::poly_utils
