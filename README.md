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
- `FRI-3` and `FRI-9` now expose a theorem-facing BCS-style PCS surface over Teichmuller-supported domains, but they remain prototype / research implementations rather than production-ready or audited FRI-based PCS code
- Current FRI openings keep `g_0` as a virtual quotient oracle, commit to `g_i` for `i >= 1`, and terminate by revealing the full final oracle table; proof-byte reporting comes from a deterministic length-prefixed serializer over that actual external opening/proof object
- `STIR(9->3)` now keeps two distinct parameter surfaces over the same external proof shape:
  - a prototype fixed-parameter STIR mode kept for compatibility and regression anchoring
  - a theorem-facing conservative GR-STIR mode used by the live benchmark path
- Both STIR modes keep the same proof-only public surface built around `initial_root`, per-round `g_root + betas + ans_polynomial + shake_polynomial + queries_to_prev`, and `queries_to_final + final_polynomial`
- The first theorem-facing STIR landing is conservative: it uses Teichmuller challenge sampling, unique-decoding exceptional-complement OOD sampling, and existing Z2KSNARK-backed GR proximity envelopes; it does not claim Johnson/list-decoding alignment or full field-paper-equivalent STIR soundness
- `poly_utils::bs08` is still a placeholder interface
- Current FRI benchmark rows expose the paper-facing repetition parameter `m`; STIR benchmark rows now execute theorem-facing parameters and emit conservative theorem-facing metadata, with unsupported conservative regimes reported as `effective_security_bits=0` plus explicit notes
- The benchmark surfaces are suitable for prototype comparisons and archived experiment evidence, not for production claims

## Paper Alignment Boundaries

Current FRI support should be read as a theorem-facing BCS-style implementation of the paper's Section 4.1 PCS contract over the repo's fixed parameter surface. It no longer uses the older public sparse-opening subset path.

Current theorem-facing FRI contract:

- verifier challenges `alpha <- T` across the full Teichmuller set, including `alpha in L`
- folding challenges `beta_i <- T`
- explicit paper-facing repetition parameter `m`
- a virtual first-round quotient oracle `g_0 = (f - v) / (X - alpha)`
- explicit committed oracle messages `g_1, ..., g_{r'}`
- a full final oracle table for `g_{r'}`

Supported now:

- a public `commit / open / verify` PCS surface over Teichmuller-supported domains
- Merkle commitments for `f` and for each explicit folded oracle `g_i`, `i >= 1`
- Fiat-Shamir-replayed folding challenges and theorem-facing repetition parameter `m`
- a virtual `g_0` relation checked against committed `f|L`, including the `alpha in L` first-round exception path
- zero-fold openings that reveal the full committed oracle table so the verifier can reconstruct the virtual terminal `g_0` locally, without an extra explicit quotient-table message
- terminal verifier-side interpolation and degree checking from the revealed final oracle table

Soundness interpretation:

- For the standalone FRI PCS path, this repository should be read as a reference implementation of the paper's BCS-style protocol surface over the repo's fixed parameter choices.
- Because the implemented protocol flow matches that paper-facing BCS version, the implementation should be understood as inheriting the paper's soundness bound under the same assumptions, rather than as requiring a separate repo-specific soundness theorem for deployment claims.
- This repository still does not target production deployment or audited cryptographic assurance; its role is protocol reference, experimentation, and measurement.

Current STIR support is split into two modes over the same `StirProof` shape:

- Prototype fixed-parameter STIR mode:
  - kept in `StirParameters` for backwards-compatible regression coverage
  - preserves the older ambient-ring challenge and prototype OOD route
  - should be read as an engineering / research prototype, not as a theorem-facing claim
- Theorem-facing conservative GR-STIR mode:
  - selected by `protocol_mode = theorem_gr_conservative`
  - uses `alpha, beta <- T`-style Teichmuller challenge sampling
  - uses exceptional-safe-complement OOD and shake sampling inside `T*` in a unique-decoding regime
  - requires theorem validation that live round domains stay inside `T*`
  - keeps the existing external `StirProof` message shape rather than redesigning the public proof format

Current theorem-facing STIR contract:

- fixed `9 -> 3` folding route only
- theorem-mode folding and comb challenges sampled from `T`
- theorem-mode OOD and shake points sampled from an explicit exceptional safe complement over `T*`
- theorem-mode validation rejects non-`T*` round domains and exhausted theorem OOD pools
- theorem-mode soundness metadata comes from `analyze_theorem_soundness(...)` and the conservative `theorem_gr_conservative_existing_z2ksnark_results` model
- unsupported conservative regimes remain visible in benchmark output, but they report `effective_security_bits=0` rather than a fabricated theorem claim

STIR soundness interpretation:

- The theorem-facing STIR landing should be read as a conservative GR adaptation of the STIR round structure, not as a paper-complete reproduction of the finite-field STIR soundness appendix.
- The implemented theorem metadata keeps `epsilon_out = 0` only in the unique-decoding OOD regime, and uses assumption-backed conservative GR folding and degree-correction envelopes derived from existing Z2KSNARK proximity results.
- This repository therefore does not claim Johnson/list-decoding alignment or full field-paper-equivalent STIR soundness.
- The prototype STIR mode remains available in code, but current `stir9to3` benchmark rows execute the theorem-facing conservative mode and describe only that theorem-facing metadata.

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
  --fri-soundness-mode manual_repetition \
  --fri-repetitions 2 \
  --soundness 128:22:ConjectureCapacity:auto
```

Benchmark notes:

- For single-thread comparisons, pin both `OMP_NUM_THREADS=1` and `--threads 1`
- `bench_time` is the end-to-end timing surface for prover / verifier behavior and exact serializer-backed proof-byte accounting
- Current `fri3` / `fri9` rows already run `commit + open + verify`; `prover_total_ms` includes both `commit` and `open`
- `serialized_bytes_actual` is computed from the deterministic serializer of the actual external message shape rather than from hand-written compact payload formulas
- Current FRI rows count the prover reply opening message (`value + opening proof`) rather than `commitment + opening` combined bytes; `alpha` remains verifier-chosen context and is not charged to the prover reply
- Current STIR rows count the exact serialized bytes of the public `StirProof`; the optional `prove_with_witness()` compatibility carrier is not charged
- Standalone FRI PCS now defaults to `--fri-soundness-mode theorem_auto`, which solves the minimum theorem-facing repetition count `m` from `lambda_target` using `max(s*ell/2^r, (1-delta)^m) <= 2^-lambda`
- Explicit `--fri-repetitions` on theorem_auto rows are treated as overrides that must be at least the required minimum `m`; otherwise `bench_time` fails fast
- `--fri-soundness-mode manual_repetition` keeps a caller-provided fixed `m`, but those rows are intentionally emitted as `soundness_mode=manual_standalone_fri` rather than as theorem-aligned metadata
- The current theorem_auto path is conservative: it is only enabled for `p=2`, and it rejects instances where the discrete gap gives `delta = 0`
- `bench_time` still keeps one shared row schema across `FRI` and `STIR`; theorem-aligned standalone FRI rows now use `lambda_target` and `effective_security_bits`, while manual standalone FRI rows leave those fields as not applicable
- Current `stir9to3` rows execute `theorem_gr_conservative` STIR parameters and emit conservative theorem-facing metadata backed by the existing Z2KSNARK-based GR envelope; unsupported parameter sets still print a row, but they are marked through `soundness_notes` and report `effective_security_bits=0`
- Current preset wrappers and parameter-search tooling preserve older fixed-`m` benchmark presets by mapping them to `manual_repetition`; omit `fri_repetitions` if you want theorem-auto standalone FRI rows
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
