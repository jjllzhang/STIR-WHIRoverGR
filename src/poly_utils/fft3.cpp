#include "poly_utils/fft3.hpp"

#include <NTL/ZZ_pE.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(SWGR_HAS_OPENMP)
#include <omp.h>
#endif

using NTL::clear;
using NTL::power;

namespace swgr::poly_utils {
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
    const std::vector<algebra::GRElem>& coefficients,
    const algebra::GRElem& x) {
  algebra::GRElem acc;
  clear(acc);
  for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
    acc *= x;
    acc += *it;
  }
  return acc;
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
  const algebra::GRContext* ctx = nullptr;
  algebra::GRElem inv_three;
  std::vector<Radix3Stage> stages;
};

struct Radix3DecodedEvals {
  std::vector<algebra::GRElem> mod0;
  std::vector<algebra::GRElem> mod1;
  std::vector<algebra::GRElem> mod2;
};

Radix3Plan BuildRadix3Plan(const Domain& domain) {
  Radix3Plan plan;
  plan.ctx = &domain.context();
  plan.inv_three = domain.context().inv(ConstantFromUint64(domain.context(), 3));

  std::uint64_t current_size = domain.size();
  algebra::GRElem current_offset = domain.offset();
  algebra::GRElem current_root = domain.root();

  while (true) {
    Radix3Stage stage;
    stage.size = current_size;
    stage.offset = current_offset;

    if (current_size > 1) {
      stage.reduced_size = current_size / 3;
      stage.offset_inv = domain.context().inv(current_offset);
      stage.root = current_root;
      stage.root_inv = domain.context().inv(current_root);
      stage.zeta = power(current_root, CheckedLong(stage.reduced_size, "reduced_size"));
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

std::vector<algebra::GRElem> SplitByResidue(
    const std::vector<algebra::GRElem>& values, std::size_t residue) {
  const std::size_t out_size = (values.size() + 2U - residue) / 3U;
  std::vector<algebra::GRElem> out;
  out.reserve(out_size);
  for (std::size_t index = residue; index < values.size(); index += 3U) {
    out.push_back(values[index]);
  }
  return out;
}

int CurrentMaxThreads() {
#if defined(SWGR_HAS_OPENMP)
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

std::vector<algebra::GRElem> ForwardCombineRadix3StageSerial(
    const Radix3Stage& stage,
    const std::vector<algebra::GRElem>& evals_mod0,
    const std::vector<algebra::GRElem>& evals_mod1,
    const std::vector<algebra::GRElem>& evals_mod2) {
  std::vector<algebra::GRElem> out(static_cast<std::size_t>(stage.size));
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
  return out;
}

std::vector<algebra::GRElem> ForwardCombineRadix3Stage(
    const Radix3Plan& plan, const Radix3Stage& stage,
    const std::vector<algebra::GRElem>& evals_mod0,
    const std::vector<algebra::GRElem>& evals_mod1,
    const std::vector<algebra::GRElem>& evals_mod2) {
  if (!ShouldParallelizeRadix3Stage(stage.reduced_size)) {
    return ForwardCombineRadix3StageSerial(stage, evals_mod0, evals_mod1,
                                           evals_mod2);
  }

  const int max_threads = CurrentMaxThreads();
  const std::ptrdiff_t reduced_count =
      CheckedPtrdiff(stage.reduced_size, "radix3 reduced_size");
  const std::ptrdiff_t chunk_size =
      ChooseRadix3ChunkSize(stage.reduced_size, max_threads);
  if (reduced_count <= chunk_size) {
    return ForwardCombineRadix3StageSerial(stage, evals_mod0, evals_mod1,
                                           evals_mod2);
  }

  std::vector<algebra::GRElem> out(static_cast<std::size_t>(stage.size));
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

  return out;
}

Radix3DecodedEvals InverseDecodeRadix3StageSerial(
    const Radix3Plan& plan, const Radix3Stage& stage,
    const std::vector<algebra::GRElem>& evals) {
  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  Radix3DecodedEvals decoded{
      .mod0 = std::vector<algebra::GRElem>(reduced_size),
      .mod1 = std::vector<algebra::GRElem>(reduced_size),
      .mod2 = std::vector<algebra::GRElem>(reduced_size),
  };

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

    decoded.mod0[index] = b0;
    decoded.mod1[index] = b1 * x_inv;
    decoded.mod2[index] = b2 * x_sq_inv;

    x_inv *= stage.root_inv;
  }

  return decoded;
}

Radix3DecodedEvals InverseDecodeRadix3Stage(
    const Radix3Plan& plan, const Radix3Stage& stage,
    const std::vector<algebra::GRElem>& evals) {
  if (!ShouldParallelizeRadix3Stage(stage.reduced_size)) {
    return InverseDecodeRadix3StageSerial(plan, stage, evals);
  }

  const int max_threads = CurrentMaxThreads();
  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  const std::ptrdiff_t reduced_count =
      CheckedPtrdiff(stage.reduced_size, "radix3 reduced_size");
  const std::ptrdiff_t chunk_size =
      ChooseRadix3ChunkSize(stage.reduced_size, max_threads);
  if (reduced_count <= chunk_size) {
    return InverseDecodeRadix3StageSerial(plan, stage, evals);
  }

  Radix3DecodedEvals decoded{
      .mod0 = std::vector<algebra::GRElem>(reduced_size),
      .mod1 = std::vector<algebra::GRElem>(reduced_size),
      .mod2 = std::vector<algebra::GRElem>(reduced_size),
  };

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

          decoded.mod0[index] = b0;
          decoded.mod1[index] = b1 * x_inv;
          decoded.mod2[index] = b2 * x_sq_inv;

          x_inv *= stage.root_inv;
        }
      });

  return decoded;
}

std::vector<algebra::GRElem> InverseScatterRadix3StageSerial(
    const Radix3Stage& stage,
    const std::vector<algebra::GRElem>& coeffs_mod0,
    const std::vector<algebra::GRElem>& coeffs_mod1,
    const std::vector<algebra::GRElem>& coeffs_mod2) {
  std::vector<algebra::GRElem> coefficients(static_cast<std::size_t>(stage.size));
  for (std::uint64_t index = 0; index < stage.reduced_size; ++index) {
    const std::size_t coeff_index = static_cast<std::size_t>(index);
    coefficients[3U * coeff_index] = coeffs_mod0[coeff_index];
    coefficients[3U * coeff_index + 1U] = coeffs_mod1[coeff_index];
    coefficients[3U * coeff_index + 2U] = coeffs_mod2[coeff_index];
  }
  return coefficients;
}

std::vector<algebra::GRElem> InverseScatterRadix3Stage(
    const Radix3Plan& plan, const Radix3Stage& stage,
    const std::vector<algebra::GRElem>& coeffs_mod0,
    const std::vector<algebra::GRElem>& coeffs_mod1,
    const std::vector<algebra::GRElem>& coeffs_mod2) {
  if (!ShouldParallelizeRadix3Stage(stage.reduced_size)) {
    return InverseScatterRadix3StageSerial(stage, coeffs_mod0, coeffs_mod1,
                                           coeffs_mod2);
  }

  const int max_threads = CurrentMaxThreads();
  const std::ptrdiff_t reduced_count =
      CheckedPtrdiff(stage.reduced_size, "radix3 reduced_size");
  const std::ptrdiff_t chunk_size =
      ChooseRadix3ChunkSize(stage.reduced_size, max_threads);
  if (reduced_count <= chunk_size) {
    return InverseScatterRadix3StageSerial(stage, coeffs_mod0, coeffs_mod1,
                                           coeffs_mod2);
  }

  std::vector<algebra::GRElem> coefficients(static_cast<std::size_t>(stage.size));
  plan.ctx->parallel_for_chunks_with_ntl_context(
      reduced_count, chunk_size, true,
      [&](std::ptrdiff_t begin, std::ptrdiff_t end) {
        for (std::ptrdiff_t index = begin; index < end; ++index) {
          const std::size_t coeff_index = static_cast<std::size_t>(index);
          coefficients[3U * coeff_index] = coeffs_mod0[coeff_index];
          coefficients[3U * coeff_index + 1U] = coeffs_mod1[coeff_index];
          coefficients[3U * coeff_index + 2U] = coeffs_mod2[coeff_index];
        }
      });

  return coefficients;
}

std::vector<algebra::GRElem> ForwardRadix3(
    const Radix3Plan& plan, std::size_t level,
    const std::vector<algebra::GRElem>& coefficients) {
  const auto& stage = plan.stages[level];
  if (stage.size == 1) {
    return {EvaluateCoefficientsAt(coefficients, stage.offset)};
  }

  const auto coeffs_mod0 = SplitByResidue(coefficients, 0);
  const auto coeffs_mod1 = SplitByResidue(coefficients, 1);
  const auto coeffs_mod2 = SplitByResidue(coefficients, 2);

  const auto evals_mod0 = ForwardRadix3(plan, level + 1U, coeffs_mod0);
  const auto evals_mod1 = ForwardRadix3(plan, level + 1U, coeffs_mod1);
  const auto evals_mod2 = ForwardRadix3(plan, level + 1U, coeffs_mod2);

  return ForwardCombineRadix3Stage(plan, stage, evals_mod0, evals_mod1,
                                   evals_mod2);
}

std::vector<algebra::GRElem> InverseRadix3(
    const Radix3Plan& plan, std::size_t level,
    const std::vector<algebra::GRElem>& evals) {
  const auto& stage = plan.stages[level];
  if (stage.size == 1) {
    return evals;
  }

  auto decoded = InverseDecodeRadix3Stage(plan, stage, evals);

  const auto coeffs_mod0 = InverseRadix3(plan, level + 1U, decoded.mod0);
  const auto coeffs_mod1 = InverseRadix3(plan, level + 1U, decoded.mod1);
  const auto coeffs_mod2 = InverseRadix3(plan, level + 1U, decoded.mod2);

  return InverseScatterRadix3Stage(plan, stage, coeffs_mod0, coeffs_mod1,
                                   coeffs_mod2);
}

}  // namespace

std::vector<algebra::GRElem> fft3(const Domain& domain,
                                  const Polynomial& poly) {
  if (!IsPowerOfThree(domain.size())) {
    throw std::invalid_argument("poly_utils::fft3 requires a 3-smooth domain");
  }

  return domain.context().with_ntl_context([&] {
    const auto plan = BuildRadix3Plan(domain);
    return ForwardRadix3(plan, 0, poly.coefficients());
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
    const auto plan = BuildRadix3Plan(domain);
    return InverseRadix3(plan, 0, evals);
  });
}

}  // namespace swgr::poly_utils
