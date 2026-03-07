#include "poly_utils/fft3.hpp"

#include <NTL/ZZ_pE.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using NTL::clear;
using NTL::power;

namespace swgr::poly_utils {
namespace {

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

  std::vector<algebra::GRElem> out(static_cast<std::size_t>(stage.size));
  algebra::GRElem x = stage.offset;
  for (std::uint64_t base = 0; base < stage.reduced_size; ++base) {
    const std::size_t index = static_cast<std::size_t>(base);
    const algebra::GRElem& eval0 = evals_mod0[index];
    const algebra::GRElem& eval1 = evals_mod1[index];
    const algebra::GRElem& eval2 = evals_mod2[index];

    const algebra::GRElem x_sq = x * x;
    const algebra::GRElem x_zeta = x * stage.zeta;
    const algebra::GRElem x_zeta_sq = x * stage.zeta_sq;

    out[index] = eval0 + x * eval1 + x_sq * eval2;
    out[index + static_cast<std::size_t>(stage.reduced_size)] =
        eval0 + x_zeta * eval1 + (x_sq * stage.zeta_sq) * eval2;
    out[index + 2U * static_cast<std::size_t>(stage.reduced_size)] =
        eval0 + x_zeta_sq * eval1 + (x_sq * stage.zeta) * eval2;

    x *= stage.root;
  }
  return out;
}

std::vector<algebra::GRElem> InverseRadix3(
    const Radix3Plan& plan, std::size_t level,
    const std::vector<algebra::GRElem>& evals) {
  const auto& stage = plan.stages[level];
  if (stage.size == 1) {
    return evals;
  }

  const std::size_t reduced_size = static_cast<std::size_t>(stage.reduced_size);
  std::vector<algebra::GRElem> evals_mod0(reduced_size);
  std::vector<algebra::GRElem> evals_mod1(reduced_size);
  std::vector<algebra::GRElem> evals_mod2(reduced_size);

  algebra::GRElem x_inv = stage.offset_inv;
  for (std::uint64_t base = 0; base < stage.reduced_size; ++base) {
    const std::size_t index = static_cast<std::size_t>(base);
    const auto& value0 = evals[index];
    const auto& value1 = evals[index + reduced_size];
    const auto& value2 = evals[index + 2U * reduced_size];

    const algebra::GRElem b0 =
        (value0 + value1 + value2) * plan.inv_three;
    const algebra::GRElem b1 =
        (value0 + stage.zeta_sq * value1 + stage.zeta * value2) * plan.inv_three;
    const algebra::GRElem b2 =
        (value0 + stage.zeta * value1 + stage.zeta_sq * value2) * plan.inv_three;
    const algebra::GRElem x_sq_inv = x_inv * x_inv;

    evals_mod0[index] = b0;
    evals_mod1[index] = b1 * x_inv;
    evals_mod2[index] = b2 * x_sq_inv;

    x_inv *= stage.root_inv;
  }

  const auto coeffs_mod0 = InverseRadix3(plan, level + 1U, evals_mod0);
  const auto coeffs_mod1 = InverseRadix3(plan, level + 1U, evals_mod1);
  const auto coeffs_mod2 = InverseRadix3(plan, level + 1U, evals_mod2);

  std::vector<algebra::GRElem> coefficients(static_cast<std::size_t>(stage.size));
  for (std::uint64_t index = 0; index < stage.reduced_size; ++index) {
    const std::size_t coeff_index = static_cast<std::size_t>(index);
    coefficients[3U * coeff_index] = coeffs_mod0[coeff_index];
    coefficients[3U * coeff_index + 1U] = coeffs_mod1[coeff_index];
    coefficients[3U * coeff_index + 2U] = coeffs_mod2[coeff_index];
  }
  return coefficients;
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
