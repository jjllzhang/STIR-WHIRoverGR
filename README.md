# STIR & WHIR over Galois Rings

`STIR&WHIRoverGR` is a **cryptographic protocol prototype repository** for experimenting with low-degree testing and commitment-oriented components over Galois rings. The current codebase explores how `FRI / STIR / WHIR`-style ideas over finite fields can be adapted to the `GR(p^k, r)` setting, with an emphasis on implementation boundaries, reusable interfaces, and benchmark methodology.

The public focus of this repository is:

- algebra and domain infrastructure over Galois rings,
- prototype prover / verifier paths for `FRI-3`, `FRI-9`, and `STIR(9->3)`,
- a WHIR-over-GR unique-decoding PCS prototype following `whir_gr2k_pcs.pdf`,
- transcript, Merkle, multiproof, benchmark, and parameter-search tooling around those protocol experiments.

This repository is **prototype / research code**. It is intended for protocol exploration, implementation experiments, and measurement. It should not be treated as production-ready or audited cryptographic software.

## Current Scope

- Vendors only `third_party/GaloisRing/` as the underlying Galois-ring backend
- Implements `GRContext`, ring-element serialization, and Teichmuller subgroup / coset domains
- Implements protocol helpers such as polynomials, interpolation, quotient polynomials, degree correction, and folding
- Uses radix-3 `fft3` / `inverse_fft3` fast paths on `3-smooth` domains, including `rs_encode` / `rs_interpolate`
- Implements `BLAKE3`, Fiat-Shamir transcript, Merkle tree, and pruned multiproof planning
- Provides prover / verifier surfaces for `FRI-3`, `FRI-9`, and `STIR(9->3)`
- WHIR-over-GR unique-decoding PCS prototype is implemented for `GR(2^s,r)` with ternary domains, multi-quadratic polynomials, explicit multilinear-polynomial embedding, BCS-style Merkle commitments, Fiat-Shamir sumcheck/folding/opening, and selector-backed unique-decoding metadata
- Provides `bench_time`, preset-driven wrappers, and parameter-search scripts

## Current Limits

- WHIR support is the GR unique-decoding PCS prototype from `whir_gr2k_pcs.pdf`; it is not full finite-field WHIR, not Johnson/list-decoding WHIR, not ZK WHIR, and not a production-ready cryptographic library
- WHIR currently targets `p=2` Galois rings, uses direct-enumeration sumcheck/prover helpers for correctness, and should be benchmarked first with small `lambda=32/64` smoke parameters before larger release-style runs
- WHIR multilinear inputs are embedded into the ternary multi-quadratic code path; the selector still reports the conservative `3^m` soundness dimension rather than a separate optimized multilinear rate
- `FRI-3` and `FRI-9` now expose a theorem-facing BCS-style PCS surface over Teichmuller-supported domains, but they remain prototype / research implementations rather than production-ready or audited FRI-based PCS code
- Current FRI openings keep `g_0` as a virtual quotient oracle, commit to `g_i` for `i >= 1`, and terminate by revealing the full final oracle table; proof-byte reporting comes from a deterministic length-prefixed serializer over that actual external opening/proof object
- `STIR(9->3)` now keeps two distinct parameter surfaces over the same external proof shape:
  - a prototype fixed-parameter STIR mode kept for compatibility and regression anchoring
  - a theorem-facing GR-STIR mode used by the live benchmark path
- Both STIR modes keep the same proof-only public surface built around `initial_root`, per-round `g_root + betas + ans_polynomial + shake_polynomial + queries_to_prev`, and `queries_to_final + final_polynomial`
- The first theorem-facing STIR landing is half-gap based: it uses Teichmuller challenge sampling, unique-decoding exceptional-complement OOD sampling, and existing Z2KSNARK-backed `(1-rho)/2` GR proximity terms; it does not claim Johnson/list-decoding alignment or full field-paper-equivalent STIR soundness
- `poly_utils::bs08` is still a placeholder interface
- Current FRI benchmark rows expose the paper-facing repetition parameter `m`; STIR benchmark rows now execute theorem-facing parameters and emit theorem-facing half-gap metadata, with unsupported theorem_gr regimes reported as `effective_security_bits=0` plus explicit notes
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
- Theorem-facing GR-STIR mode:
  - selected by `protocol_mode = theorem_gr`
  - uses `alpha, beta <- T`-style Teichmuller challenge sampling
  - uses exceptional-safe-complement OOD and shake sampling inside `T*` in a unique-decoding regime
  - requires theorem validation that live round domains stay inside `T*`
  - keeps the existing external `StirProof` message shape rather than redesigning the public proof format

Current theorem-facing STIR contract:

- fixed `9 -> 3` folding route only
- theorem-mode folding and comb challenges sampled from `T`
- theorem-mode OOD and shake points sampled from an explicit exceptional safe complement over `T*`
- theorem-mode validation rejects non-`T*` round domains and exhausted theorem OOD pools
- theorem-mode soundness metadata comes from `analyze_theorem_soundness(...)` and the `theorem_gr_existing_z2ksnark_half_gap_results` model
- unsupported theorem_gr regimes remain visible in benchmark output, but they report `effective_security_bits=0` rather than a fabricated theorem claim

STIR soundness interpretation:

- The theorem-facing STIR landing should be read as a half-gap GR adaptation of the STIR round structure, not as a paper-complete reproduction of the finite-field STIR soundness appendix.
- The implemented theorem metadata keeps `epsilon_out = 0` only in the unique-decoding OOD regime, and uses half-gap GR folding and degree-correction terms derived from existing Z2KSNARK proximity results.
- This repository therefore does not claim Johnson/list-decoding alignment or full field-paper-equivalent STIR soundness.
- The prototype STIR mode remains available in code, but current `stir9to3` benchmark rows execute the theorem-facing `theorem_gr` mode and describe only that theorem-facing metadata.

Current WHIR-over-GR support follows the repository-local `whir_gr2k_pcs.pdf`
scope rather than the full finite-field `WHIR.pdf` modes.

Implemented WHIR-over-GR unique-decoding PCS contract:

- public parameter selection from `(lambda, s, m, bmax, rho0)` using the WHIR-UD half-gap recipe `delta_i=(1-rho_i)/2`, with an optional fixed extension degree `r`
- `Setup`-style construction of `GR(2^s,r)`, a Teichmuller subgroup `H0`, an order-3 grid `B={1,omega,omega^2}`, layer widths, repetitions, degree bounds, and soundness metadata
- `Commit` to the table `f0[x] = F(Pow_m(x))` for a multi-quadratic polynomial `F`
- `Commit` also accepts a multilinear polynomial and embeds it by zeroing the ternary quadratic coefficient positions
- `Open` with ternary sumcheck, repeated ternary virtual folds, shifted oracle commitments on `H_i.pow_map(3)`, shift queries over `H_i.pow_map(3^b)`, and final constant-oracle openings
- `Verify` with transcript replay, degree and sumcheck identity checks, exact Merkle multiproof query matching, virtual-fold recomputation from opened leaves, final constraint check, and final constant openings

WHIR soundness interpretation:

- Benchmark rows for `whir_gr_ud` report `soundness_mode=theorem_whir_gr_unique_decoding`, `soundness_model=epsilon_iopp_whir_gr_unique_decoding`, and `soundness_scope=whir_gr2k_pcs_unique_decoding`.
- The implementation intentionally does not claim WHIR-JB / WHIR-CB list-decoding, Johnson-bound modes, OOD uniqueness from the finite-field WHIR paper, or production cryptographic assurance.

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
ctest --test-dir build --output-on-failure -R 'test_gr_basic|test_domain|test_fft3|test_folding|test_crypto|test_fri|test_stir|test_whir|test_soundness_configurator'
```

These tests cover:

- core Galois-ring semantics and serialization,
- domain / Teichmuller construction,
- `fft3` and `folding` correctness,
- transcript / Merkle / multiproof behavior,
- honest / tamper regressions for `FRI-3`, `FRI-9`, `STIR(9->3)`, and WHIR-over-GR unique-decoding PCS,
- basic soundness-configurator output behavior.

## Benchmarks

Inspect the benchmark CLIs:

```bash
./build-release/bench_time --help
```

Run a WHIR-over-GR unique-decoding smoke benchmark:

```bash
./build-release/bench_time \
  --protocol whir_gr_ud \
  --lambda 64 \
  --k-exp 16 \
  --whir-r 108 \
  --whir-m 3 \
  --whir-bmax 1 \
  --whir-rho0 1/3 \
  --whir-polynomial multilinear \
  --warmup 0 \
  --reps 1 \
  --format text
```

Run a benchmark preset:

```bash
OMP_NUM_THREADS=1 ./scripts/bench/run_bench.sh \
  --preset bench/presets/fri.json \
  --build-dir build-release \
  --threads 1 \
  --warmup 1 \
  --reps 3
```

Run only part of a multi-experiment preset:

```bash
OMP_NUM_THREADS=1 ./scripts/bench/run_bench.sh \
  --preset bench/presets/whir.json \
  --build-dir build-release \
  --experiments gr216_r162_m4_multilinear,gr216_r162_m10_multilinear \
  --threads 1 \
  --warmup 0 \
  --reps 1
```

Run parameter search:

```bash
python3 scripts/bench/search_params.py \
  --preset bench/presets/stir.json \
  --build-dir build-release \
  --n-values 81,243 \
  --rho-values 1/3,1/9 \
  --soundness 128:22:ConjectureCapacity:auto
```

Benchmark notes:

- For single-thread comparisons, pin both `OMP_NUM_THREADS=1` and `--threads 1`
- `bench_time` is the end-to-end timing surface for prover / verifier behavior and exact serializer-backed proof-byte accounting
- `bench_stir_query_solver` solves a theorem-facing explicit STIR query schedule from `lambda_target` under the current `theorem_gr` half-gap model, and reports infeasible targets explicitly
- Current `fri3` / `fri9` rows already run `commit + open + verify`; `prover_total_ms` includes both `commit` and `open`
- `serialized_bytes_actual` is computed from the deterministic serializer of the actual external message shape rather than from hand-written compact payload formulas
- Current FRI rows count the prover reply opening message (`value + opening proof`) rather than `commitment + opening` combined bytes; `alpha` remains verifier-chosen context and is not charged to the prover reply
- Current STIR rows count the exact serialized bytes of the public `StirProof`; the optional `prove_with_witness()` compatibility carrier is not charged
- Standalone FRI PCS now defaults to `--fri-soundness-mode theorem_auto`, which solves the minimum theorem-facing repetition count `m` from `lambda_target` using `max(s*ell/2^r, (1-delta)^m) <= 2^-lambda`
- Explicit `--fri-repetitions` on theorem_auto rows are treated as overrides that must be at least the required minimum `m`; otherwise `bench_time` fails fast
- `--fri-soundness-mode manual_repetition` keeps a caller-provided fixed `m`, but those rows are intentionally emitted as `soundness_mode=manual_standalone_fri` rather than as theorem-aligned metadata
- The current theorem_auto path is conservative: it is only enabled for `p=2`, and it rejects instances where the discrete gap gives `delta = 0`
- `bench_time` still keeps one shared row schema across `FRI`, `STIR`, and `WHIR`; theorem-aligned standalone FRI rows now use `lambda_target` and `effective_security_bits`, while manual standalone FRI rows leave those fields as not applicable
- Current `stir9to3` rows execute `theorem_gr` STIR parameters and emit theorem-facing half-gap metadata backed by the existing Z2KSNARK-based GR results; unsupported parameter sets still print a row, but they are marked through `soundness_notes` and report `effective_security_bits=0`
- Current `whir_gr_ud` rows execute commit/open/verify for the WHIR-over-GR unique-decoding PCS prototype, choose `r` from the selector by default, accept `--whir-r`/`--whir-fixed-r` or explicit `--r` to fix `GR(2^s,r)`, and report exact serialized opening bytes from the actual `WhirOpening`
- `--whir-repetitions` is a debug override for shift/final repetitions; selector-derived repetitions are used by default for theorem-facing metadata
- STIR now has three query modes in `bench_time`: `--queries auto` keeps the older heuristic live schedule, `--queries theorem_auto` solves an explicit per-round schedule from `lambda_target` using the current theorem_gr half-gap model, and `--queries q0[,q1,...]` keeps a caller-provided manual schedule
- The standalone `bench_stir_query_solver` tool exposes the same theorem-driven STIR query solver without running prover/verifier timing loops; use it when the question is feasibility or minimal query counts rather than runtime
- Current preset-runner and parameter-search tooling preserve older fixed-`m` benchmark presets by mapping them to `manual_repetition`; omit `fri_repetitions` if you want theorem-auto standalone FRI rows
- Preset schema and benchmark file layout are documented in `bench/README.md`
- Archived benchmark outputs live in `results/archive/`; new timing/search outputs default to ignored `results/runs/<timestamp>/` directories

## Preset Workloads

Current preset filenames follow a shorter workload-first naming style:

- `bench/presets/fri.json`
- `bench/presets/stir.json`
- `bench/presets/whir.json`

Interpretation:

- `fri.json` contains standalone FRI workloads, including `GR(2^16,54)` smoke
  and `GR(2^16,162)` main-size examples
- `stir.json` contains STIR workloads, including `GR(2^16,162)` main-size and
  `GR(2^16,486)` two-round examples
- `whir.json` contains WHIR unique-decoding multilinear workloads. The
  `GR(2^16,162)` experiments default to 128-bit security and cover
  `whir_m=4..10`; the older `GR(2^16,54)` smoke experiment remains an explicit
  32-bit override.

## CMake Targets

Main targets:

- `galoisring_backend`: vendored Galois-ring backend static library
- `stir_over_gr`: main project static library
- aliases: `swgr::galoisring_backend`, `swgr::stir_over_gr`, `swgr::swgr`
- `bench_time`: end-to-end timing benchmark for FRI, STIR, and `whir_gr_ud`
- focused WHIR tests: `test_whir`, `test_whir_multiquadratic`, `test_whir_constraint`, `test_whir_folding`, `test_whir_soundness`, `test_whir_roundtrip`

## Repository Layout

- `include/`: public headers and protocol interfaces
- `src/`: implementations
- `bench/`: benchmark entrypoints, preset schema documentation, and presets
- `scripts/bench/`: benchmark helper scripts for preset runs, parameter search, and plots
- `tests/`: unit tests and protocol regressions
- `results/archive/`: curated benchmark CSV archives
- `results/runs/`: ignored generated benchmark runs
- `third_party/GaloisRing/`: vendored Galois-ring backend
- `third_party/blake3/`: vendored `BLAKE3`
