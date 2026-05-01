# Benchmark Layout

This directory contains the repository's public benchmark entrypoints and
workload presets. Current benchmark binaries are built by CMake when
`STIR_WHIR_GR_BUILD_BENCH=ON`. Helper scripts live in `scripts/bench/`.

## Binaries

- `bench_time.cpp`: end-to-end timing and proof-byte accounting for
  `fri3`, `fri9`, `stir9to3`, and `whir_gr_ud`.
- `bench_stir_query_solver.cpp`: standalone theorem-facing STIR query-schedule
  feasibility solver. It does not run prover/verifier timing loops.
- `bench_common.hpp`: shared CLI parsing and output formatting helpers.

## Presets

Preset files live in `bench/presets/`. They are JSON workload descriptions read
by `scripts/bench/run_bench.sh` and `scripts/bench/search_params.py`.

Presets may either be a single workload object or a multi-workload object:

- `defaults`: optional shared field object.
- `experiments`: optional list of workload objects. Each experiment inherits
  `defaults`, then overrides any field it sets.
- `name`: optional experiment label used by helper scripts and search output.

Use `--experiments` to run only a subset of a preset. The selector accepts
experiment names, 1-based indices, and index ranges:

```bash
./scripts/bench/run_bench.sh \
  --preset bench/presets/whir.json \
  --experiments gr216_r162_m4_multilinear,gr216_r162_m10_multilinear

python3 scripts/bench/search_params.py \
  --preset bench/presets/whir.json \
  --experiments 2,4-6
```

Common fields:

- `ring`: required; format `GR(p^k,r)`.
- `protocols`: list of protocols, or `["all"]`; supported values are `fri3`,
  `fri9`, `stir9to3`, and `whir_gr_ud`.
- `n`, `d`, `rho`: domain size, degree/dimension, and displayed rate. FRI/STIR
  use `n` and `d` directly. WHIR recomputes its live `n`, `d`, and `rho` from
  the selector, but these fields keep preset/search schemas uniform.
- `lambda_target`, `pow_bits`, `sec_mode`, `hash_profile`: benchmark metadata
  and soundness/security knobs.
- `threads`: default thread count for wrappers.

FRI fields:

- `fri_soundness_mode`: `theorem_auto` or `manual_repetition`.
- `fri_repetitions`: required for `manual_repetition`; optional lower-bound
  override for `theorem_auto`.

STIR fields:

- `stop_degree`
- `ood_samples`
- `queries`: `auto`, `theorem_auto`, or an explicit list such as `[2, 1]`.

WHIR fields:

- `whir_m`: source variable count / ternary selector dimension exponent.
- `whir_bmax`: maximum layer width.
- `whir_r` or `whir_fixed_r`: fixed extension degree for `GR(2^s,r)`.
- `whir_rho0`: initial rate, for example `1/3`.
- `whir_polynomial`: `multiquadratic` or `multilinear`.
- `whir_repetitions`: optional debug override for shift/final repetitions.

The wrappers always pass the preset ring's `r` through `bench_time --r`. For
WHIR this is a fixed-extension fallback, so WHIR presets should set `whir_r`
explicitly when the fixed ring is part of the workload definition.

For WHIR, `whir_rho0` is the selector input. The displayed `rho` is the actual
selected `d/n` after the fixed ring and Teichmuller-domain divisibility
constraints are applied, so it can differ across experiments even when
`whir_rho0` is shared.

## Results

Tracked, curated benchmark archives live under `results/archive/`.

Timing-preset and parameter-search outputs default to
`results/runs/<timestamp>/` and are ignored by git. Keep temporary searches,
one-off timing runs, plots, logs, and profiler artifacts there unless they are
intentionally promoted into `results/archive/`.

Do not add a `results/README.md`; benchmark documentation belongs here and in
the top-level `README.md`.

## Naming

Keep one short preset per benchmark family:

- `fri.json`
- `stir.json`
- `whir.json`

Avoid names that only encode a phase number, batch number, or vague status such
as `preset0`, `small`, `release`, or `latest`.
