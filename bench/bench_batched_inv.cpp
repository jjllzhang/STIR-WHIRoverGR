#include "bench_common.hpp"

#include <NTL/ZZ_pE.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "GaloisRing/Inverse.hpp"
#include "algebra/gr_context.hpp"
#include "domain.hpp"
#include "poly_utils/folding.hpp"
#include "poly_utils/interpolation.hpp"
#include "poly_utils/polynomial.hpp"

using NTL::ZZ_pE;
using NTL::clear;
using NTL::deg;
using NTL::set;

namespace {

volatile std::uint64_t g_sink = 0;

void Consume(std::uint64_t value) {
  g_sink = g_sink + value;
}

struct BatchedInvBenchOptions {
  std::uint64_t p = 2;
  std::uint64_t k_exp = 16;
  std::uint64_t r = 162;
  std::uint64_t n = 243;
  std::uint64_t d = 81;
  std::uint64_t fold = 9;
  std::uint64_t warmup = 1;
  std::uint64_t reps = 1;
  std::uint64_t interpolate_iters = 1;
  std::uint64_t fold_iters = 1;
  swgr::bench::OutputFormat format = swgr::bench::OutputFormat::Text;
};

struct BatchedInvBenchRow {
  std::string operation;
  std::string ring;
  std::string baseline_mode;
  std::string batched_mode;
  std::uint64_t n = 0;
  std::uint64_t d = 0;
  std::uint64_t fold = 0;
  std::uint64_t calls_per_iteration = 0;
  std::uint64_t points_per_call = 0;
  std::uint64_t iterations_per_rep = 0;
  std::uint64_t warmup = 0;
  std::uint64_t reps = 0;
  std::uint64_t baseline_inversions_per_iteration = 0;
  std::uint64_t batched_inversions_per_iteration = 0;
  double baseline_mean_ms = 0.0;
  double batched_mean_ms = 0.0;
  double speedup_x = 0.0;
  std::uint64_t checksum_delta = 0;
};

struct InterpolationDataset {
  std::vector<swgr::algebra::GRElem> points;
  std::vector<swgr::algebra::GRElem> values;
};

struct FoldDataset {
  std::vector<std::vector<swgr::algebra::GRElem>> fibers;
  std::vector<std::vector<swgr::algebra::GRElem>> values;
  std::vector<swgr::algebra::GRElem> alphas;
};

long CheckedLong(std::uint64_t value, const char* label) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<long>::max())) {
    throw std::invalid_argument(std::string(label) + " exceeds long");
  }
  return static_cast<long>(value);
}

double SafeMean(double total, std::uint64_t reps) {
  return reps == 0 ? 0.0 : total / static_cast<double>(reps);
}

double ElapsedMs(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

std::string BatchedInvBenchUsage(const char* binary_name) {
  return std::string("Usage: ") + binary_name +
         " [options]\n"
         "  --p <uint> --k-exp <uint> --r <uint>\n"
         "  --n <uint> --d <uint> --fold <uint>\n"
         "  --warmup <uint> --reps <uint>\n"
         "  --interpolate-iters <uint> --fold-iters <uint>\n"
         "  --format text|csv|json\n";
}

BatchedInvBenchOptions ParseOptions(int argc, char** argv) {
  BatchedInvBenchOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--help" || argument == "-h") {
      continue;
    }

    std::string key;
    std::string value;
    const std::size_t equals = argument.find('=');
    if (equals == std::string::npos) {
      key = argument;
      if (i + 1 >= argc) {
        throw std::invalid_argument("missing value after " + key);
      }
      value = argv[++i];
    } else {
      key = argument.substr(0, equals);
      value = argument.substr(equals + 1);
    }

    if (key == "--p") {
      options.p = swgr::bench::ParseUint64(key, value);
    } else if (key == "--k-exp") {
      options.k_exp = swgr::bench::ParseUint64(key, value);
    } else if (key == "--r") {
      options.r = swgr::bench::ParseUint64(key, value);
    } else if (key == "--n") {
      options.n = swgr::bench::ParseUint64(key, value);
    } else if (key == "--d") {
      options.d = swgr::bench::ParseUint64(key, value);
    } else if (key == "--fold") {
      options.fold = swgr::bench::ParseUint64(key, value);
    } else if (key == "--warmup") {
      options.warmup = swgr::bench::ParseUint64(key, value);
    } else if (key == "--reps") {
      options.reps = swgr::bench::ParseUint64(key, value);
    } else if (key == "--interpolate-iters") {
      options.interpolate_iters = swgr::bench::ParseUint64(key, value);
    } else if (key == "--fold-iters") {
      options.fold_iters = swgr::bench::ParseUint64(key, value);
    } else if (key == "--format") {
      options.format = swgr::bench::ParseOutputFormat(value);
    } else {
      throw std::invalid_argument("unknown option: " + key);
    }
  }

  if (options.n == 0 || options.d >= options.n) {
    throw std::invalid_argument("bench_batched_inv requires 0 < d < n");
  }
  if (options.fold == 0 || options.n % options.fold != 0) {
    throw std::invalid_argument("bench_batched_inv requires fold dividing n");
  }
  if (options.reps == 0) {
    throw std::invalid_argument("--reps must be > 0");
  }
  if (options.interpolate_iters == 0) {
    throw std::invalid_argument("--interpolate-iters must be > 0");
  }
  if (options.fold_iters == 0) {
    throw std::invalid_argument("--fold-iters must be > 0");
  }
  return options;
}

swgr::algebra::GRElem EncodeUnsigned(std::uint64_t value) {
  swgr::algebra::GRElem result;
  clear(result);
  swgr::algebra::GRElem addend;
  set(addend);
  while (value > 0) {
    if ((value & 1U) != 0U) {
      result += addend;
    }
    value >>= 1U;
    if (value != 0) {
      addend += addend;
    }
  }
  return result;
}

std::vector<swgr::algebra::GRElem> Trim(
    std::vector<swgr::algebra::GRElem> coefficients) {
  while (!coefficients.empty() && coefficients.back() == 0) {
    coefficients.pop_back();
  }
  return coefficients;
}

std::vector<swgr::algebra::GRElem> MultiplyByMonicLinear(
    const std::vector<swgr::algebra::GRElem>& coefficients,
    const swgr::algebra::GRElem& root) {
  std::vector<swgr::algebra::GRElem> next(coefficients.size() + 1U);
  for (auto& coefficient : next) {
    clear(coefficient);
  }
  for (std::size_t i = 0; i < coefficients.size(); ++i) {
    next[i] -= coefficients[i] * root;
    next[i + 1U] += coefficients[i];
  }
  return next;
}

std::vector<swgr::algebra::GRElem> DerivativeCoefficients(
    const std::vector<swgr::algebra::GRElem>& coefficients) {
  if (coefficients.size() <= 1U) {
    return {};
  }

  std::vector<swgr::algebra::GRElem> derivative(coefficients.size() - 1U);
  for (std::size_t i = 1; i < coefficients.size(); ++i) {
    derivative[i - 1U] = coefficients[i] * EncodeUnsigned(i);
  }
  return Trim(std::move(derivative));
}

swgr::algebra::GRElem EvaluatePolynomialCoefficients(
    const std::vector<swgr::algebra::GRElem>& coefficients,
    const swgr::algebra::GRElem& point) {
  swgr::algebra::GRElem acc;
  clear(acc);
  for (auto it = coefficients.rbegin(); it != coefficients.rend(); ++it) {
    acc *= point;
    acc += *it;
  }
  return acc;
}

std::vector<swgr::algebra::GRElem> DivideByMonicLinear(
    const std::vector<swgr::algebra::GRElem>& coefficients,
    const swgr::algebra::GRElem& root) {
  if (coefficients.size() <= 1U) {
    return {};
  }

  const std::size_t quotient_size = coefficients.size() - 1U;
  std::vector<swgr::algebra::GRElem> quotient(quotient_size);
  quotient[quotient_size - 1U] = coefficients.back();
  for (std::size_t i = quotient_size - 1U; i > 0; --i) {
    quotient[i - 1U] = coefficients[i] + root * quotient[i];
  }
  return Trim(std::move(quotient));
}

swgr::poly_utils::Polynomial SamplePolynomial(
    const swgr::algebra::GRContext& ctx, const swgr::Domain& domain,
    std::size_t coefficient_count) {
  return ctx.with_ntl_context([&] {
    std::vector<swgr::algebra::GRElem> coefficients;
    coefficients.reserve(coefficient_count);

    auto root_power = ctx.one();
    for (std::size_t i = 0; i < coefficient_count; ++i) {
      coefficients.push_back(root_power + ctx.one());
      root_power *= domain.root();
    }
    coefficients.back() += ctx.one();
    return swgr::poly_utils::Polynomial(std::move(coefficients));
  });
}

bool SamePolynomial(const swgr::poly_utils::Polynomial& lhs,
                    const swgr::poly_utils::Polynomial& rhs) {
  return lhs.coefficients() == rhs.coefficients();
}

swgr::poly_utils::Polynomial InterpolateSingleInv(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& points,
    const std::vector<swgr::algebra::GRElem>& values) {
  if (points.empty()) {
    throw std::invalid_argument("InterpolateSingleInv requires points");
  }
  if (points.size() != values.size()) {
    throw std::invalid_argument("InterpolateSingleInv requires equal-sized inputs");
  }

  return ctx.with_ntl_context([&] {
    const long extension_degree = CheckedLong(ctx.config().r, "r");
    std::vector<swgr::algebra::GRElem> vanishing;
    vanishing.reserve(points.size() + 1U);
    vanishing.emplace_back();
    set(vanishing.front());
    for (const auto& point : points) {
      vanishing = MultiplyByMonicLinear(vanishing, point);
    }

    const auto derivative = DerivativeCoefficients(vanishing);
    std::vector<swgr::algebra::GRElem> barycentric_weights;
    barycentric_weights.reserve(points.size());
    for (const auto& point : points) {
      const auto derivative_value = EvaluatePolynomialCoefficients(derivative, point);
      const auto inverse = Inv(derivative_value, extension_degree);
      if (inverse == 0) {
        throw std::invalid_argument(
            "interpolate_for_gr_wrapper requires an exceptional point set");
      }
      barycentric_weights.push_back(inverse);
    }

    std::vector<swgr::algebra::GRElem> coefficients(points.size());
    for (auto& coefficient : coefficients) {
      clear(coefficient);
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
      const auto basis = DivideByMonicLinear(vanishing, points[i]);
      const auto scale = values[i] * barycentric_weights[i];
      for (std::size_t j = 0; j < basis.size(); ++j) {
        coefficients[j] += basis[j] * scale;
      }
    }
    return swgr::poly_utils::Polynomial(Trim(std::move(coefficients)));
  });
}

bool LooksLikeStructuredFiber(
    const std::vector<swgr::algebra::GRElem>& fiber_points) {
  if (fiber_points.size() <= 1U) {
    return true;
  }

  const long extension_degree = deg(ZZ_pE::modulus());
  const swgr::algebra::GRElem first_inverse =
      Inv(fiber_points.front(), extension_degree);
  if (first_inverse == 0) {
    return false;
  }

  const auto candidate_root = fiber_points[1] * first_inverse;
  swgr::algebra::GRElem current_power;
  set(current_power);
  for (std::size_t i = 0; i < fiber_points.size(); ++i) {
    if (fiber_points[i] != fiber_points.front() * current_power) {
      return false;
    }
    current_power *= candidate_root;
  }
  swgr::algebra::GRElem one;
  set(one);
  return current_power == one;
}

swgr::algebra::GRElem FoldEvalSingleInvInCurrentContext(
    const std::vector<swgr::algebra::GRElem>& fiber_points,
    const std::vector<swgr::algebra::GRElem>& fiber_values,
    const swgr::algebra::GRElem& alpha) {
  if (fiber_points.empty()) {
    throw std::invalid_argument("fold_eval_k requires non-empty fiber");
  }
  if (fiber_points.size() != fiber_values.size()) {
    throw std::invalid_argument("fold_eval_k requires equal-sized fiber inputs");
  }

  const long extension_degree = deg(ZZ_pE::modulus());
  const std::size_t fiber_size = fiber_points.size();
  std::vector<swgr::algebra::GRElem> differences(fiber_size);
  std::vector<swgr::algebra::GRElem> prefix_products(fiber_size + 1U);
  std::vector<swgr::algebra::GRElem> suffix_products(fiber_size + 1U);
  std::vector<swgr::algebra::GRElem> denominator_inverses(fiber_size);

  for (std::size_t i = 0; i < fiber_size; ++i) {
    differences[i] = alpha - fiber_points[i];
    swgr::algebra::GRElem denominator_product;
    set(denominator_product);
    for (std::size_t j = 0; j < fiber_size; ++j) {
      if (i == j) {
        continue;
      }
      denominator_product *= fiber_points[i] - fiber_points[j];
    }
    const auto inverse = Inv(denominator_product, extension_degree);
    if (inverse == 0) {
      throw std::invalid_argument("fold_eval_k requires exceptional fiber");
    }
    denominator_inverses[i] = inverse;
  }

  set(prefix_products[0]);
  for (std::size_t i = 0; i < fiber_size; ++i) {
    prefix_products[i + 1U] = prefix_products[i] * differences[i];
  }

  set(suffix_products[fiber_size]);
  for (std::size_t i = fiber_size; i > 0; --i) {
    suffix_products[i - 1U] = suffix_products[i] * differences[i - 1U];
  }

  swgr::algebra::GRElem result;
  clear(result);
  for (std::size_t i = 0; i < fiber_size; ++i) {
    const auto numerator = prefix_products[i] * suffix_products[i + 1U];
    result += fiber_values[i] * numerator * denominator_inverses[i];
  }
  return result;
}

swgr::algebra::GRElem FoldEvalSingleInv(
    const swgr::algebra::GRContext& ctx,
    const std::vector<swgr::algebra::GRElem>& fiber_points,
    const std::vector<swgr::algebra::GRElem>& fiber_values,
    const swgr::algebra::GRElem& alpha) {
  return ctx.with_ntl_context(
      [&] { return FoldEvalSingleInvInCurrentContext(fiber_points, fiber_values, alpha); });
}

InterpolationDataset BuildInterpolationDataset(
    const swgr::algebra::GRContext& ctx, const swgr::Domain& domain,
    const swgr::poly_utils::Polynomial& poly) {
  return ctx.with_ntl_context([&] {
    const auto shift = EncodeUnsigned(2);
    const auto domain_points = domain.elements();

    InterpolationDataset dataset;
    dataset.points.reserve(domain_points.size());
    dataset.values.reserve(domain_points.size());
    for (const auto& point : domain_points) {
      const auto shifted_point = point + shift;
      dataset.points.push_back(shifted_point);
      dataset.values.push_back(poly.evaluate(ctx, shifted_point));
    }
    return dataset;
  });
}

FoldDataset BuildFoldDataset(const swgr::algebra::GRContext& ctx,
                             const swgr::Domain& domain,
                             const swgr::poly_utils::Polynomial& poly,
                             std::uint64_t fold) {
  return ctx.with_ntl_context([&] {
    const auto shift = EncodeUnsigned(2);
    const std::uint64_t fiber_count = domain.size() / fold;
    FoldDataset dataset;
    dataset.fibers.resize(static_cast<std::size_t>(fiber_count));
    dataset.values.resize(static_cast<std::size_t>(fiber_count));
    dataset.alphas.resize(static_cast<std::size_t>(fiber_count));

    for (std::uint64_t base = 0; base < fiber_count; ++base) {
      auto& fiber = dataset.fibers[static_cast<std::size_t>(base)];
      auto& values = dataset.values[static_cast<std::size_t>(base)];
      fiber.reserve(static_cast<std::size_t>(fold));
      values.reserve(static_cast<std::size_t>(fold));

      for (std::uint64_t offset = 0; offset < fold; ++offset) {
        const std::uint64_t index = base + offset * fiber_count;
        const auto point = domain.element(index) + shift;
        fiber.push_back(point);
        values.push_back(poly.evaluate(ctx, point));
      }
      if (LooksLikeStructuredFiber(fiber)) {
        throw std::runtime_error(
            "bench_batched_inv expected generic fold fibers but built a structured one");
      }
      dataset.alphas[static_cast<std::size_t>(base)] = fiber.front() + ctx.one();
    }
    return dataset;
  });
}

template <typename Fn>
double MeasureMeanMs(std::uint64_t warmup, std::uint64_t reps, Fn&& fn) {
  for (std::uint64_t i = 0; i < warmup; ++i) {
    fn();
  }

  double total_ms = 0.0;
  for (std::uint64_t i = 0; i < reps; ++i) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    total_ms += ElapsedMs(start, end);
  }
  return SafeMean(total_ms, reps);
}

BatchedInvBenchRow RunInterpolationBench(
    const BatchedInvBenchOptions& options, const swgr::algebra::GRContext& ctx,
    const InterpolationDataset& dataset) {
  const auto baseline_poly =
      InterpolateSingleInv(ctx, dataset.points, dataset.values);
  const auto batched_poly =
      swgr::poly_utils::interpolate_for_gr_wrapper(ctx, dataset.points,
                                                   dataset.values);
  if (!SamePolynomial(baseline_poly, batched_poly)) {
    throw std::runtime_error(
        "bench_batched_inv interpolation variants disagree on coefficients");
  }

  const std::uint64_t checksum_before = g_sink;
  const double baseline_mean_ms = MeasureMeanMs(
      options.warmup, options.reps, [&] {
        for (std::uint64_t iter = 0; iter < options.interpolate_iters; ++iter) {
          const auto poly = InterpolateSingleInv(ctx, dataset.points, dataset.values);
          Consume(static_cast<std::uint64_t>(poly.coefficients().size()));
        }
      });
  const double batched_mean_ms = MeasureMeanMs(
      options.warmup, options.reps, [&] {
        for (std::uint64_t iter = 0; iter < options.interpolate_iters; ++iter) {
          const auto poly = swgr::poly_utils::interpolate_for_gr_wrapper(
              ctx, dataset.points, dataset.values);
          Consume(static_cast<std::uint64_t>(poly.coefficients().size()));
        }
      });
  const std::uint64_t checksum_after = g_sink;

  BatchedInvBenchRow row;
  row.operation = "interpolate_for_gr_wrapper";
  row.ring = swgr::bench::RingString(options.p, options.k_exp, options.r);
  row.baseline_mode = "same_barycentric_single_inv";
  row.batched_mode = "public_wrapper_batch_inv";
  row.n = options.n;
  row.d = options.d;
  row.fold = options.fold;
  row.calls_per_iteration = 1;
  row.points_per_call = options.n;
  row.iterations_per_rep = options.interpolate_iters;
  row.warmup = options.warmup;
  row.reps = options.reps;
  row.baseline_inversions_per_iteration = options.n;
  row.batched_inversions_per_iteration = 1;
  row.baseline_mean_ms = baseline_mean_ms;
  row.batched_mean_ms = batched_mean_ms;
  row.speedup_x =
      batched_mean_ms == 0.0 ? 0.0 : baseline_mean_ms / batched_mean_ms;
  row.checksum_delta = checksum_after - checksum_before;
  return row;
}

BatchedInvBenchRow RunFoldBench(const BatchedInvBenchOptions& options,
                                const swgr::algebra::GRContext& ctx,
                                const FoldDataset& dataset) {
  for (std::size_t i = 0; i < dataset.fibers.size(); ++i) {
    const auto baseline = FoldEvalSingleInv(ctx, dataset.fibers[i],
                                            dataset.values[i], dataset.alphas[i]);
    const auto batched = ctx.with_ntl_context([&] {
      return swgr::poly_utils::fold_eval_k(dataset.fibers[i], dataset.values[i],
                                           dataset.alphas[i]);
    });
    if (baseline != batched) {
      throw std::runtime_error(
          "bench_batched_inv fold variants disagree on evaluation result");
    }
  }

  const std::uint64_t checksum_before = g_sink;
  const double baseline_mean_ms = MeasureMeanMs(
      options.warmup, options.reps, [&] {
        ctx.with_ntl_context([&] {
          const auto zero = ctx.zero();
          for (std::uint64_t iter = 0; iter < options.fold_iters; ++iter) {
            for (std::size_t fiber_index = 0; fiber_index < dataset.fibers.size();
                 ++fiber_index) {
              const auto value = FoldEvalSingleInvInCurrentContext(
                  dataset.fibers[fiber_index], dataset.values[fiber_index],
                  dataset.alphas[fiber_index]);
              Consume(static_cast<std::uint64_t>(value != zero));
            }
          }
          return 0;
        });
      });
  const double batched_mean_ms = MeasureMeanMs(
      options.warmup, options.reps, [&] {
        ctx.with_ntl_context([&] {
          const auto zero = ctx.zero();
          for (std::uint64_t iter = 0; iter < options.fold_iters; ++iter) {
            for (std::size_t fiber_index = 0; fiber_index < dataset.fibers.size();
                 ++fiber_index) {
              const auto value = swgr::poly_utils::fold_eval_k(
                  dataset.fibers[fiber_index], dataset.values[fiber_index],
                  dataset.alphas[fiber_index]);
              Consume(static_cast<std::uint64_t>(value != zero));
            }
          }
          return 0;
        });
      });
  const std::uint64_t checksum_after = g_sink;

  BatchedInvBenchRow row;
  row.operation = "fold_eval_k_generic";
  row.ring = swgr::bench::RingString(options.p, options.k_exp, options.r);
  row.baseline_mode = "same_denominator_product_single_inv";
  row.batched_mode = "public_fold_eval_k_batch_inv";
  row.n = options.n;
  row.d = options.d;
  row.fold = options.fold;
  row.calls_per_iteration = options.n / options.fold;
  row.points_per_call = options.fold;
  row.iterations_per_rep = options.fold_iters;
  row.warmup = options.warmup;
  row.reps = options.reps;
  row.baseline_inversions_per_iteration = options.n;
  row.batched_inversions_per_iteration = options.n / options.fold;
  row.baseline_mean_ms = baseline_mean_ms;
  row.batched_mean_ms = batched_mean_ms;
  row.speedup_x =
      batched_mean_ms == 0.0 ? 0.0 : baseline_mean_ms / batched_mean_ms;
  row.checksum_delta = checksum_after - checksum_before;
  return row;
}

void PrintText(const std::vector<BatchedInvBenchRow>& rows) {
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i];
    std::cout << "operation=" << row.operation << "\n"
              << "ring=" << row.ring << "\n"
              << "baseline_mode=" << row.baseline_mode << "\n"
              << "batched_mode=" << row.batched_mode << "\n"
              << "n=" << row.n << "\n"
              << "d=" << row.d << "\n"
              << "fold=" << row.fold << "\n"
              << "calls_per_iteration=" << row.calls_per_iteration << "\n"
              << "points_per_call=" << row.points_per_call << "\n"
              << "iterations_per_rep=" << row.iterations_per_rep << "\n"
              << "warmup=" << row.warmup << "\n"
              << "reps=" << row.reps << "\n"
              << "baseline_inversions_per_iteration="
              << row.baseline_inversions_per_iteration << "\n"
              << "batched_inversions_per_iteration="
              << row.batched_inversions_per_iteration << "\n"
              << std::fixed << std::setprecision(3)
              << "baseline_mean_ms=" << row.baseline_mean_ms << "\n"
              << "batched_mean_ms=" << row.batched_mean_ms << "\n"
              << "speedup_x=" << row.speedup_x << "\n"
              << "checksum_delta=" << row.checksum_delta << "\n";
    if (i + 1U != rows.size()) {
      std::cout << "\n";
    }
  }
}

void PrintCsv(const std::vector<BatchedInvBenchRow>& rows) {
  std::cout
      << "operation,ring,baseline_mode,batched_mode,n,d,fold,calls_per_iteration,points_per_call,"
         "iterations_per_rep,warmup,reps,baseline_inversions_per_iteration,"
         "batched_inversions_per_iteration,baseline_mean_ms,batched_mean_ms,"
         "speedup_x,checksum_delta\n";
  for (const auto& row : rows) {
    std::cout << row.operation << "," << row.ring << ","
              << row.baseline_mode << "," << row.batched_mode << ","
              << row.n << "," << row.d << "," << row.fold << ","
              << row.calls_per_iteration << "," << row.points_per_call << ","
              << row.iterations_per_rep << "," << row.warmup << ","
              << row.reps << ","
              << row.baseline_inversions_per_iteration << ","
              << row.batched_inversions_per_iteration << "," << std::fixed
              << std::setprecision(3) << row.baseline_mean_ms << ","
              << row.batched_mean_ms << "," << row.speedup_x << ","
              << row.checksum_delta << "\n";
  }
}

void PrintJson(const std::vector<BatchedInvBenchRow>& rows) {
  std::cout << "[\n";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i];
    std::cout << "  {\n"
              << "    \"operation\": \"" << row.operation << "\",\n"
              << "    \"ring\": \"" << row.ring << "\",\n"
              << "    \"baseline_mode\": \"" << row.baseline_mode << "\",\n"
              << "    \"batched_mode\": \"" << row.batched_mode << "\",\n"
              << "    \"n\": " << row.n << ",\n"
              << "    \"d\": " << row.d << ",\n"
              << "    \"fold\": " << row.fold << ",\n"
              << "    \"calls_per_iteration\": " << row.calls_per_iteration
              << ",\n"
              << "    \"points_per_call\": " << row.points_per_call << ",\n"
              << "    \"iterations_per_rep\": " << row.iterations_per_rep
              << ",\n"
              << "    \"warmup\": " << row.warmup << ",\n"
              << "    \"reps\": " << row.reps << ",\n"
              << "    \"baseline_inversions_per_iteration\": "
              << row.baseline_inversions_per_iteration << ",\n"
              << "    \"batched_inversions_per_iteration\": "
              << row.batched_inversions_per_iteration << ",\n"
              << std::fixed << std::setprecision(3)
              << "    \"baseline_mean_ms\": " << row.baseline_mean_ms << ",\n"
              << "    \"batched_mean_ms\": " << row.batched_mean_ms << ",\n"
              << "    \"speedup_x\": " << row.speedup_x << ",\n"
              << "    \"checksum_delta\": " << row.checksum_delta << "\n"
              << "  }";
    if (i + 1U != rows.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (swgr::bench::WantsHelp(argc, argv)) {
      std::cout << BatchedInvBenchUsage(argv[0]);
      return 0;
    }

    const auto options = ParseOptions(argc, argv);
    const swgr::algebra::GRContext ctx(
        swgr::algebra::GRConfig{.p = options.p,
                                .k_exp = options.k_exp,
                                .r = options.r});
    const swgr::Domain domain = swgr::Domain::teichmuller_subgroup(ctx, options.n);
    const auto poly = SamplePolynomial(ctx, domain,
                                       static_cast<std::size_t>(options.d + 1U));
    const auto interpolation_dataset = BuildInterpolationDataset(ctx, domain, poly);
    const auto fold_dataset = BuildFoldDataset(ctx, domain, poly, options.fold);

    std::vector<BatchedInvBenchRow> rows;
    rows.push_back(RunInterpolationBench(options, ctx, interpolation_dataset));
    rows.push_back(RunFoldBench(options, ctx, fold_dataset));

    switch (options.format) {
      case swgr::bench::OutputFormat::Text:
        PrintText(rows);
        break;
      case swgr::bench::OutputFormat::Csv:
        PrintCsv(rows);
        break;
      case swgr::bench::OutputFormat::Json:
        PrintJson(rows);
        break;
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "bench_batched_inv failed: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "bench_batched_inv failed: unknown exception\n";
    return 1;
  }
}
