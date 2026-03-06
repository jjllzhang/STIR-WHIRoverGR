#include "poly_utils/fft3.hpp"

#include <NTL/ZZ_pE.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

std::vector<algebra::GRElem> SliceCoefficientsByResidue(
    const std::vector<algebra::GRElem>& coefficients, std::size_t residue) {
  std::vector<algebra::GRElem> sliced;
  for (std::size_t index = residue; index < coefficients.size(); index += 3U) {
    sliced.push_back(coefficients[index]);
  }
  return sliced;
}

algebra::GRElem ConstantFromUint64(const algebra::GRContext& ctx,
                                   std::uint64_t value) {
  return ctx.with_ntl_context([&] {
    algebra::GRElem one = ctx.one();
    algebra::GRElem out(0);
    for (std::uint64_t i = 0; i < value; ++i) {
      out += one;
    }
    return out;
  });
}

std::vector<algebra::GRElem> ForwardRadix3(
    const Domain& domain, const std::vector<algebra::GRElem>& coefficients) {
  const std::uint64_t domain_size = domain.size();
  const auto& ctx = domain.context();
  if (domain_size == 1) {
    return {Polynomial(coefficients).evaluate(ctx, domain.offset())};
  }

  const std::uint64_t reduced_size = domain_size / 3;
  const Domain folded_domain = domain.pow_map(3);
  const auto coeffs_mod0 = SliceCoefficientsByResidue(coefficients, 0);
  const auto coeffs_mod1 = SliceCoefficientsByResidue(coefficients, 1);
  const auto coeffs_mod2 = SliceCoefficientsByResidue(coefficients, 2);

  const auto evals_mod0 = ForwardRadix3(folded_domain, coeffs_mod0);
  const auto evals_mod1 = ForwardRadix3(folded_domain, coeffs_mod1);
  const auto evals_mod2 = ForwardRadix3(folded_domain, coeffs_mod2);

  std::vector<algebra::GRElem> out(static_cast<std::size_t>(domain_size));
  return ctx.with_ntl_context([&] {
    const algebra::GRElem zeta =
        power(domain.root(), CheckedLong(reduced_size, "reduced_size"));
    const algebra::GRElem zeta_sq = zeta * zeta;
    algebra::GRElem x = domain.offset();

    for (std::uint64_t base = 0; base < reduced_size; ++base) {
      const algebra::GRElem x_sq = x * x;
      const algebra::GRElem x_zeta = x * zeta;
      const algebra::GRElem x_zeta_sq = x * zeta_sq;

      out[static_cast<std::size_t>(base)] =
          evals_mod0[static_cast<std::size_t>(base)] +
          x * evals_mod1[static_cast<std::size_t>(base)] +
          x_sq * evals_mod2[static_cast<std::size_t>(base)];
      out[static_cast<std::size_t>(base + reduced_size)] =
          evals_mod0[static_cast<std::size_t>(base)] +
          x_zeta * evals_mod1[static_cast<std::size_t>(base)] +
          (x_zeta * x_zeta) * evals_mod2[static_cast<std::size_t>(base)];
      out[static_cast<std::size_t>(base + 2 * reduced_size)] =
          evals_mod0[static_cast<std::size_t>(base)] +
          x_zeta_sq * evals_mod1[static_cast<std::size_t>(base)] +
          (x_zeta_sq * x_zeta_sq) * evals_mod2[static_cast<std::size_t>(base)];

      x *= domain.root();
    }

    return out;
  });
}

std::vector<algebra::GRElem> InverseRadix3(
    const Domain& domain, const std::vector<algebra::GRElem>& evals) {
  const std::uint64_t domain_size = domain.size();
  const auto& ctx = domain.context();
  if (domain_size == 1) {
    return evals;
  }

  const std::uint64_t reduced_size = domain_size / 3;
  std::vector<algebra::GRElem> evals_mod0(static_cast<std::size_t>(reduced_size));
  std::vector<algebra::GRElem> evals_mod1(static_cast<std::size_t>(reduced_size));
  std::vector<algebra::GRElem> evals_mod2(static_cast<std::size_t>(reduced_size));

  ctx.with_ntl_context([&] {
    const algebra::GRElem zeta =
        power(domain.root(), CheckedLong(reduced_size, "reduced_size"));
    const algebra::GRElem zeta_sq = zeta * zeta;
    const algebra::GRElem inv_three = ctx.inv(ConstantFromUint64(ctx, 3));
    const algebra::GRElem root_inv = ctx.inv(domain.root());
    algebra::GRElem x_inv = ctx.inv(domain.offset());

    for (std::uint64_t base = 0; base < reduced_size; ++base) {
      const auto& value0 = evals[static_cast<std::size_t>(base)];
      const auto& value1 = evals[static_cast<std::size_t>(base + reduced_size)];
      const auto& value2 =
          evals[static_cast<std::size_t>(base + 2 * reduced_size)];

      const algebra::GRElem b0 = (value0 + value1 + value2) * inv_three;
      const algebra::GRElem b1 =
          (value0 + zeta_sq * value1 + zeta * value2) * inv_three;
      const algebra::GRElem b2 =
          (value0 + zeta * value1 + zeta_sq * value2) * inv_three;
      const algebra::GRElem x_sq_inv = x_inv * x_inv;

      evals_mod0[static_cast<std::size_t>(base)] = b0;
      evals_mod1[static_cast<std::size_t>(base)] = b1 * x_inv;
      evals_mod2[static_cast<std::size_t>(base)] = b2 * x_sq_inv;

      x_inv *= root_inv;
    }

    return 0;
  });

  const Domain folded_domain = domain.pow_map(3);
  const auto coeffs_mod0 = InverseRadix3(folded_domain, evals_mod0);
  const auto coeffs_mod1 = InverseRadix3(folded_domain, evals_mod1);
  const auto coeffs_mod2 = InverseRadix3(folded_domain, evals_mod2);

  std::vector<algebra::GRElem> coefficients(static_cast<std::size_t>(domain_size));
  for (std::uint64_t index = 0; index < reduced_size; ++index) {
    coefficients[static_cast<std::size_t>(3 * index)] =
        coeffs_mod0[static_cast<std::size_t>(index)];
    coefficients[static_cast<std::size_t>(3 * index + 1)] =
        coeffs_mod1[static_cast<std::size_t>(index)];
    coefficients[static_cast<std::size_t>(3 * index + 2)] =
        coeffs_mod2[static_cast<std::size_t>(index)];
  }
  return coefficients;
}

}  // namespace

std::vector<algebra::GRElem> fft3(const Domain& domain,
                                  const Polynomial& poly) {
  if (!IsPowerOfThree(domain.size())) {
    throw std::invalid_argument("poly_utils::fft3 requires a 3-smooth domain");
  }
  return ForwardRadix3(domain, poly.coefficients());
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

  return InverseRadix3(domain, evals);
}

}  // namespace swgr::poly_utils
