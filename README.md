# STIR & WHIR over Galois Rings

`STIR&WHIRoverGR` is a **cryptographic protocol prototype repository** for experimenting with low-degree testing and commitment-oriented components over Galois rings. The current codebase explores how `FRI / STIR / WHIR`-style ideas over finite fields can be adapted to the `GR(p^k, r)` setting, with an emphasis on implementation boundaries, reusable interfaces, and benchmark methodology.

The public focus of this repository is:

- algebra and domain infrastructure over Galois rings,
- prototype prover / verifier paths for `FRI-3`, `FRI-9`, and `STIR(9->3)`,
- transcript, Merkle, multiproof, benchmark, and parameter-search tooling around those protocol experiments.

This repository is **prototype / research code**. It is intended for protocol exploration, implementation experiments, and measurement. It should not be treated as production-ready or audited cryptographic software.

## Current Scope

- Vendors only `third_party/GaloisRing/` as the underlying Galois-ring backend
- Implements `GRContext`, ring-element serialization, and Teichmuller subgroup / coset domains
- Implements protocol helpers such as polynomials, interpolation, quotient polynomials, degree correction, and folding
- Uses radix-3 `fft3` / `inverse_fft3` fast paths on `3-smooth` domains, including `rs_encode` / `rs_interpolate`
- Implements `BLAKE3`, Fiat-Shamir transcript, Merkle tree, and pruned multiproof planning
- Provides prover / verifier surfaces for `FRI-3`, `FRI-9`, and `STIR(9->3)`
- Provides `bench_time`, preset-driven wrappers, and parameter-search scripts

## Current Limits

- `WHIR` currently remains an interface-level skeleton; `src/whir/prover.cpp` and `src/whir/verifier.cpp` are still unimplemented
- `FRI-3` and `FRI-9` now expose a sparse-opening PCS surface over Teichmuller-supported domains, but they remain prototype implementations rather than theorem-4.1-complete, production-ready FRI-based PCS code
- Current FRI openings terminate with `final_polynomial` plus terminal sparse checks; proof-byte reporting now comes from a deterministic length-prefixed serializer over the actual external opening/proof messages
- `STIR(9->3)` now exposes a proof-only public surface built around `initial_root`, per-round `g_root + betas + ans_polynomial + shake_polynomial + queries_to_prev`, and `queries_to_final + final_polynomial`; it remains a prototype, fixed-parameter, Galois-ring adaptation rather than a theorem-level complete implementation of the paper
- `poly_utils::bs08` is still a placeholder interface
- Soundness-related outputs are currently for engineering experiments and parameter comparison, not for replacing formal security analysis
- The benchmark surfaces are suitable for prototype comparisons and archived experiment evidence, not for production claims

## Paper Alignment Boundaries

Current FRI support should be read as a paper-aligned subset of the Section 4.1 PCS semantics, not as a full `pi_FRICom` implementation.

Supported now:

- a public `commit / open / verify` PCS surface over Teichmuller-supported domains
- sparse Merkle openings with Fiat-Shamir-replayed round challenges and query positions
- a virtual first-round quotient oracle `g = (f - v) / (X - alpha)` derived from sparse openings of committed `f|L`
- terminal `final_polynomial` consistency checks rather than verifier-side full-table witness reconstruction

Not yet implemented or proven here:

- the full self-contained `pi_FRICom` procedure deferred by the paper to its full version
- a step-by-step theorem-level reproduction of all Section 4.1 details beyond the currently implemented `FRI-3` / `FRI-9` parameterized path
- a formal soundness proof for this fixed-parameter Galois-ring adaptation

Current STIR support should be read similarly: the public proof shape now aligns with Construction 5.2-style rounds and final-polynomial consistency checks, but the repository still implements a fixed-parameter Galois-ring adaptation rather than a full theorem-level reproduction of the field-model paper.

## Dependencies

Required:

- `CMake >= 3.20`
- a `C++20` compiler
- `NTL`
- `GMP`

Optional:

- `OpenMP`, if available, enables some parallel paths

Notes:

- `BLAKE3` is vendored in `third_party/blake3/`, so no extra system hash library is required for the current hash path

## Build

Development build:

```bash
cmake -S . -B build \
  -DSWGR_BUILD_TESTS=ON \
  -DSWGR_BUILD_BENCH=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Release build for performance measurements:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DSWGR_BUILD_TESTS=ON \
  -DSWGR_BUILD_BENCH=ON
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

Disable OpenMP explicitly if needed:

```bash
cmake -S . -B build -DSWGR_USE_OPENMP=OFF
```

## Quick Validation

For this repository, focused tests are more informative than relying on a single full `ctest` pass:

```bash
ctest --test-dir build --output-on-failure -R 'test_gr_basic|test_domain|test_fft3|test_folding|test_crypto|test_fri|test_stir|test_soundness_configurator'
```

These tests cover:

- core Galois-ring semantics and serialization,
- domain / Teichmuller construction,
- `fft3` and `folding` correctness,
- transcript / Merkle / multiproof behavior,
- honest / tamper regressions for `FRI-3`, `FRI-9`, and `STIR(9->3)`,
- basic soundness-configurator output behavior.

## Benchmarks

Inspect the benchmark CLIs:

```bash
./build-release/bench_time --help
```

Run the main timing workload:

```bash
OMP_NUM_THREADS=1 ./scripts/run_timing_benchmark_from_preset.sh \
  --preset bench/presets/main_benchmark_workload_gr216_r162.json \
  --build-dir build-release \
  --threads 1 \
  --warmup 1 \
  --reps 3 \
  --output results/main_benchmark_workload_gr216_r162_timing.csv
```

Run parameter search:

```bash
./scripts/run_benchmark_parameter_search.sh \
  --build-dir build-release \
  --n-values 81,243 \
  --rho-values 1/3,1/9 \
  --soundness 128:22:ConjectureCapacity:auto
```

Benchmark notes:

- For single-thread comparisons, pin both `OMP_NUM_THREADS=1` and `--threads 1`
- `bench_time` is the end-to-end timing surface for prover / verifier behavior and exact serializer-backed proof-byte accounting
- Current `fri3` / `fri9` rows already run `commit + open + verify`; `prover_total_ms` includes both `commit` and `open`
- `serialized_bytes_actual` is computed from the deterministic serializer of the actual external message shape rather than from hand-written compact payload formulas
- Current FRI rows count the prover reply opening message (`value + opening proof`) rather than `commitment + opening` combined bytes; `alpha` remains verifier-chosen context and is not charged to the prover reply
- Current STIR rows count the exact serialized bytes of the public `StirProof`; the optional `prove_with_witness()` compatibility carrier is not charged
- `soundness_model`, `soundness_scope`, `effective_security_bits`, and the `ConjectureCapacity` / `Conservative` labels are engineering calibration metadata for benchmark scheduling; they are not theorem-backed security claims or paper-complete parameter instantiations
- Archived benchmark outputs live in `results/`, with filenames aligned to workload names

## Preset Workloads

Current preset filenames follow a shorter workload-first naming style:

- `bench/presets/all_protocols_smoke_gr216_r54.json`
- `bench/presets/main_benchmark_workload_gr216_r162.json`
- `bench/presets/two_round_stir_gr216_r486.json`

Interpretation:

- `all_protocols_smoke_gr216_r54.json` is the smallest cross-protocol smoke workload
- `main_benchmark_workload_gr216_r162.json` is the main benchmark preset
- `two_round_stir_gr216_r486.json` is the larger preset that exercises two STIR rounds

## CMake Targets

Main targets:

- `galoisring_backend`: vendored Galois-ring backend static library
- `stir_over_gr`: main project static library
- aliases: `swgr::galoisring_backend`, `swgr::stir_over_gr`, `swgr::swgr`

## Repository Layout

- `include/`: public headers and protocol interfaces
- `src/`: implementations
- `bench/`: benchmark entrypoints and presets
- `tests/`: unit tests and protocol regressions
- `scripts/`: benchmark wrappers, parameter search, and helper scripts
- `results/`: archived benchmark outputs
- `third_party/GaloisRing/`: vendored Galois-ring backend
- `third_party/blake3/`: vendored `BLAKE3`
