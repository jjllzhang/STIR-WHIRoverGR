# WHIR-over-GR Performance Optimization Plan

This plan targets the current `whir_gr_ud` unique-decoding WHIR-over-GR
prototype in this repository. It is deliberately scoped to performance without
changing the protocol transcript, public parameter selector, proof shape, or
soundness metadata.

## Current Baseline

The useful public benchmark surface is:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 16 --target bench_time
./build-release/bench_time --protocol whir_gr_ud \
  --lambda 64 --k-exp 16 --whir-m 3 --whir-bmax 1 --whir-rho0 1/3 \
  --whir-polynomial multiquadratic --hash-profile WHIR_NATIVE \
  --warmup 0 --reps 1 --format text
```

Release baseline observed on the current checkout:

| Case | Threads | Prover total | Interpolate / sumcheck | Commit | Verify | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `m=3`, `lambda=64`, `rho0=1/3` | 1 | 9749.842 ms | 8993.655 ms | 121.015 ms | 720.411 ms | 32360 |
| `m=3`, same row | 16 | 9794.991 ms | 9045.832 ms | 121.071 ms | 724.199 ms | 32360 |
| `m=4`, same row | 1 | timed out at 180 s | dominated by sumcheck | - | - | - |

The debug `m=4` row eventually completed and had the same shape:
`profile_prover_interpolate_total_ms` was about 216 s out of 223 s. Treat that
as hotspot evidence only, not as a release timing baseline.

Conclusion: the main blocker is `src/whir/constraint.cpp` sumcheck generation,
not Merkle hashing, transcript hashing, or full-table folding.

## Optimization Principles

1. Use release-mode benchmarks only for speed claims.
2. Keep `serialized_bytes_actual`, transcript labels, roots, query derivation,
   and verifier acceptance unchanged unless a protocol change is explicitly
   requested.
3. Optimize the largest measured bucket first. For current WHIR this is
   `profile_prover_interpolate_*`, which is the time spent producing honest
   sumcheck polynomials.
4. Keep the existing slow/easy implementation as a test oracle until the new
   algebraic kernel is proven by differential tests.
5. Do not use `--whir-repetitions` for theorem-facing performance comparisons;
   it is a debug override.

## Phase 0: Benchmark and Regression Harness

Goal: make every later speedup measurable and hard to fake.

Tasks:

1. Add a small benchmark checklist under this file or `bench/README.md` with
   exact WHIR commands for `m=3` and `m=4`.
2. Capture these columns for each run:
   `commit_ms`, `prover_total_ms`, `verify_ms`,
   `profile_prover_interpolate_mean_ms`, `profile_prover_encode_mean_ms`,
   `profile_prover_fold_mean_ms`, `profile_prover_merkle_mean_ms`,
   `serialized_bytes_actual`.
3. Add focused correctness tests before changing algorithms:
   `test_whir_constraint`, `test_whir_folding`, `test_whir_roundtrip`,
   `test_whir_multiquadratic`, and `test_whir_multilinear`.

Validation commands:

```bash
cmake --build build-release -j 16
ctest --test-dir build-release --output-on-failure \
  -R 'test_whir(_constraint|_folding|_roundtrip|_multiquadratic|_multilinear)?$'
./build-release/bench_time --protocol whir_gr_ud \
  --lambda 64 --k-exp 16 --whir-m 3 --whir-bmax 1 --whir-rho0 1/3 \
  --whir-polynomial multiquadratic --hash-profile WHIR_NATIVE \
  --warmup 1 --reps 3 --format text
```

Acceptance:

1. Focused WHIR tests pass.
2. `m=3` benchmark reproduces the baseline shape: prover time is dominated by
   `profile_prover_interpolate_*`.
3. `serialized_bytes_actual` remains `32360` for the baseline `m=3` row before
   any protocol-visible changes.

Phase 0 validation on 2026-05-02:

1. `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` completed.
2. `cmake --build build-release -j 16` completed.
3. Focused release WHIR CTest passed: 6/6 tests.
4. Baseline command with `--warmup 1 --reps 3` produced
   `serialized_bytes_actual=32360`, `prover_total_ms=9828.357`,
   `verify_ms=721.276`, and
   `profile_prover_interpolate_mean_ms=9068.714`.
5. The measured prover profile is still dominated by sumcheck generation, so
   Phase 1 remains the correct first implementation target.

## Phase 1: Replace Enumerative Sumcheck Generation

Goal: replace the current exponential point-enumeration sumcheck kernel with a
direct coefficient-space formula.

Current hot path:

1. `WhirProver::open` calls
   `honest_sumcheck_polynomial(ctx, current_polynomial, constraint, alphas)`.
2. `honest_sumcheck_polynomial` in `src/whir/constraint.cpp` picks 5
   interpolation points.
3. For each interpolation point it enumerates all `3^remaining` ternary suffix
   assignments.
4. For each assignment it calls `MultiQuadraticPolynomial::evaluate(...)` and
   `WhirConstraint::evaluate_A(...)`.
5. The 5 values are interpolated back into a degree-4 polynomial.

This is the dominant cost and scales badly from `m=3` to `m=4`.

New algorithm:

Let

```text
F(X) = sum_e a_e prod_i X_i^{e_i}, where e_i in {0,1,2}
A(X) = sum_t w_t prod_i eq_B(z_{t,i}, X_i)
```

For the current sumcheck variable `j = prefix.size()`, compute the degree-4
univariate polynomial

```text
h(T) = sum_{x_tail in B^{m-j-1}} F(prefix, T, x_tail) * A(prefix, T, x_tail)
```

directly as coefficients. For each nonzero multi-quadratic coefficient `a_e`
and each constraint term `t`, its contribution factors as:

```text
a_e * w_t
* prod_{i < j} prefix_i^{e_i} * eq_B(z_{t,i}, prefix_i)
* [T^{e_j} * eq_B(z_{t,j}, T)]
* prod_{i > j} sum_{b in B} b^{e_i} * eq_B(z_{t,i}, b)
```

The bracketed term is a degree <= 4 polynomial in `T`; all other factors are
scalars. This removes both the 5-point interpolation loop and the
`3^remaining` suffix enumeration.

Implementation tasks:

1. Keep the generic `PointEvaluator` overload as the slow reference path.
2. Specialize the `MultiQuadraticPolynomial` overload in
   `src/whir/constraint.cpp`.
3. Add small helpers:
   - compute coefficients of `eq_B(z, T)` on the fixed ternary grid;
   - multiply that quadratic by `T^0`, `T^1`, or `T^2`;
   - compute `sum_{b in B} b^degree * eq_B(z, b)` for degree 0, 1, 2.
4. Iterate over `MultiQuadraticPolynomial::coefficients()` directly.
   Avoid heap allocation from repeated `decode_base3_index(...)` calls in the
   hot loop; decode ternary digits incrementally.
5. Preserve the returned `WhirSumcheckPolynomial` coefficient order and degree
   trimming semantics expected by `evaluate_sumcheck_polynomial(...)`.
6. Add differential tests comparing the new specialized path against the old
   generic enumerative path for:
   - `m=1,2,3,4`;
   - one initial constraint term and several added shift terms;
   - prefix lengths from 0 to `m-1`;
   - points both in the ternary grid and outside it.

Expected complexity change:

```text
old: O(5 * 3^(remaining variables) * cost(F evaluation + A evaluation))
new: O(nnz(F) * number_of_constraint_terms * variable_count)
```

Acceptance:

1. All focused WHIR tests pass.
2. New differential tests pass.
3. `m=3` release `profile_prover_interpolate_mean_ms` drops by at least 5x.
4. `m=4` release row completes without timeout under 180 s.
5. Baseline `m=3` proof bytes remain `32360`.

Phase 1 validation on 2026-05-02:

1. Added exact differential tests comparing the specialized
   `MultiQuadraticPolynomial` sumcheck path against the generic enumerative
   `PointEvaluator` oracle for `m=1,2,3,4`, multiple prefix lengths, grid and
   off-grid prefixes, dense/sparse/embedded-multilinear/zero/trimmed
   polynomials, and single/multi-term/empty constraints.
2. Focused release WHIR CTest passed: 6/6 tests, plus
   `test_whir_soundness` passed separately.
3. Baseline `m=3`, `--warmup 1 --reps 3`, `--threads 1` produced
   `serialized_bytes_actual=32360`, `prover_total_ms=1574.414`,
   `verify_ms=724.340`, and
   `profile_prover_interpolate_mean_ms=813.607`.
4. The previous `m=4` timeout row completed with `--warmup 0 --reps 1`:
   `prover_total_ms=14983.909`,
   `profile_prover_interpolate_mean_ms=8680.762`, and
   `serialized_bytes_actual=131636`.

Stop rule:

If Phase 1 gives less than a 2x `m=3` prover improvement, stop and inspect the
new profile before doing lower-level micro-optimizations.

## Phase 2: Cache Constraint Factor Data

Goal: reduce repeated `eq_B`, Lagrange basis, and ring inverse work in both
prover and verifier algebra.

Current cost sources:

1. `eq_B(...)` recomputes Lagrange basis values repeatedly.
2. `WhirConstraint::restrict_prefix(...)` recomputes prefix factors for every
   term at every layer.
3. Sumcheck generation repeatedly needs the same per-term, per-variable
   quantities.

Implementation tasks:

1. Introduce a small internal cache type in `src/whir/constraint.cpp`, for
   example:

```text
EqCoordinateCache:
  eq_poly_coeffs[3]       // eq_B(z_i, T)
  grid_weight_sums[3]     // sum_{b in B} b^e * eq_B(z_i, b), e=0,1,2
```

2. Build cache data per constraint term inside the specialized sumcheck kernel.
3. Precompute fixed-prefix factors once per term:

```text
prod_{i < j} prefix_i^{e_i} * eq_B(z_i, prefix_i)
```

4. Precompute tail factors for each monomial digit pattern where useful.
5. Keep cache lifetime local to the current `GRContext` / NTL context. Do not
   publish global caches of `NTL::ZZ_pE` values.
6. Consider a later `WhirConstraint` representation change only after the local
   cache proves useful. Avoid changing public headers unless the local cache
   makes the code too contorted.

Acceptance:

1. Phase 1 tests still pass.
2. `profile_prover_interpolate_mean_ms` improves beyond Phase 1 or stays flat
   with simpler code.
3. `profile_verify_algebra_mean_ms` for the `m=3` row decreases measurably or
   is explicitly documented as unchanged.

Phase 2 validation on 2026-05-02:

1. Cached ternary-grid Lagrange polynomial data per call and added per-term
   prefix/tail digit-product tables for the coefficient-space sumcheck kernel.
   Cache lifetime remains local to the active `GRContext` / NTL context.
2. Added an explicit base-3 index split regression for
   `prefix_index = idx % 3^j`, `live_digit = (idx / 3^j) % 3`, and
   `tail_index = idx / 3^(j+1)`.
3. Focused release WHIR CTest passed: 7/7 tests.
4. Baseline `m=3`, `--warmup 1 --reps 3`, `--threads 1` produced
   `serialized_bytes_actual=32360`, `prover_total_ms=929.103`,
   `verify_ms=721.252`, and
   `profile_prover_interpolate_mean_ms=171.797`.
5. The `m=4`, `--warmup 0 --reps 1`, `--threads 1` row produced
   `prover_total_ms=8337.173`,
   `profile_prover_interpolate_mean_ms=2051.915`, and
   `serialized_bytes_actual=131636`.
6. Verifier algebra is effectively unchanged in this phase
   (`profile_verify_algebra_mean_ms=535.608` for the `m=3` row), as expected
   because the cache is local to prover sumcheck generation.

## Phase 3: Reuse Oracle and Merkle State

Goal: remove duplicated full-oracle work from `WhirProver::open`.

Current code shape:

1. `WhirProver::commit` builds `initial_oracle` and an initial Merkle tree.
2. `WhirCommitmentState` stores only `initial_oracle` and `oracle_root`.
3. `WhirProver::open` rebuilds the initial Merkle tree only to check the root
   and serve openings.
4. In each round, `open` computes `next_oracle = EncodeOracle(...)`, then also
   computes `folded_for_queries = repeated_ternary_fold_table(...)` over the
   full current oracle just to read shift positions.

Implementation tasks:

1. Extend `WhirCommitmentState` to cache the initial `MerkleTree`:

```text
std::optional<crypto::MerkleTree> initial_tree;
```

2. Move the tree from `commit` into the state and reuse it in `open`.
3. Add tests that prove state/root mismatch is still rejected.
4. Prove whether `next_oracle[shift_index]` is equal to the repeated virtual
   fold value for the same layer. If yes, remove full-table
   `repeated_ternary_fold_table(...)` from the prover and use `next_oracle` for
   `shift_values`.
5. If exact equality is not true for every current layer, add a sparse prover
   helper that computes only queried fibers, mirroring
   `evaluate_virtual_fold_query_from_leaf_payloads(...)`, instead of folding the
   whole table.

Acceptance:

1. Focused WHIR roundtrip and folding tests pass.
2. `profile_prover_merkle_mean_ms` and/or `profile_prover_fold_mean_ms`
   decreases.
3. `serialized_bytes_actual` is unchanged.

Expected impact:

This is a secondary optimization. For `m=3`, encode + fold + Merkle are small
compared with sumcheck, but after Phase 1 they may become visible.

Phase 3 validation on 2026-05-02:

1. `WhirCommitmentState` now caches the initial Merkle tree produced by
   `commit`, and `open` reuses it after checking root and leaf-count
   consistency against the commitment.
2. Added state mismatch tests for cached-tree presence and cached-tree root
   mismatch, while preserving the existing commitment-root mismatch rejection.
3. Replaced unconditional full-table virtual folding with a hybrid path:
   dense shift-query layers keep the existing full-table fold, while sparse
   layers compute only queried fibers with the same sparse fold evaluator used
   by verifier-side tests. This avoids changing transcript labels, roots,
   query derivation, or proof shape.
4. Focused release WHIR CTest passed: 7/7 tests.
5. Baseline `m=3`, `--warmup 1 --reps 3`, `--threads 1` produced
   `serialized_bytes_actual=32360`, `prover_total_ms=928.505`,
   `verify_ms=724.701`, `profile_prover_merkle_mean_ms=5.900`, and
   `profile_prover_fold_mean_ms=30.221`.
6. The `m=4`, `--warmup 0 --reps 1`, `--threads 1` row produced
   `serialized_bytes_actual=131636`, `prover_total_ms=8344.528`,
   `profile_prover_merkle_mean_ms=30.688`, and
   `profile_prover_fold_mean_ms=127.895`.

## Phase 4: Verifier Query and Sparse Fold Cleanup

Goal: reduce verifier overhead once prover sumcheck is no longer dominant.

Current verifier cost sources:

1. `PayloadsForIndices(...)` uses a linear `std::find` for each requested
   parent index.
2. It copies payload vectors into a temporary vector before folding.
3. `evaluate_virtual_fold_query_from_leaf_payloads(...)` regenerates parent
   indices and allocates point/value vectors for every query.

Implementation tasks:

1. Replace linear search with `std::lower_bound` over sorted
   `proof.queried_indices`.
2. Add a helper that reads payload references directly from the Merkle proof,
   avoiding temporary `std::vector<std::vector<uint8_t>>` copies.
3. Add a sparse folding helper that accepts already-known parent indices and
   payload spans.
4. Keep the current shape-checking behavior and invalid-proof rejection tests.

Acceptance:

1. All existing verifier tamper tests still pass.
2. `profile_verify_query_mean_ms` decreases for `m=3`.
3. `profile_verify_algebra_mean_ms` is not increased by extra conversions or
   allocations.

Phase 4 validation on 2026-05-02:

1. Replaced verifier payload lookup with `std::lower_bound` over the sorted
   Merkle proof query list after the existing exact query-set check.
2. Avoided copying leaf payload byte vectors while evaluating virtual folds;
   verifier now keeps payload references and deserializes directly into the
   sparse fold input.
3. Reused the already-computed parent index vectors for each shift query, so
   verifier query processing no longer regenerates virtual-fold indices.
4. Focused release WHIR CTest passed: 7/7 tests, including tamper/replay
   rejection in `test_whir`.
5. Baseline `m=3`, `--warmup 1 --reps 3`, `--threads 1` produced
   `serialized_bytes_actual=32360`, `verify_ms=720.029`,
   `profile_verify_query_mean_ms=131.679`, and
   `profile_verify_algebra_mean_ms=535.906`.

## Phase 5: Parallelize Only After Algorithmic Fixes

Goal: make `--threads` meaningful for WHIR only after the dominant algorithm is
not the old sequential enumerator.

Candidate loops:

1. The direct sumcheck coefficient accumulation over chunks of polynomial
   coefficients or constraint terms.
2. `EncodeOracle(...)` over domain indices.
3. Any remaining full-table fold path.

Implementation rules:

1. Use `GRContext::parallel_for_chunks_with_ntl_context(...)`; do not use raw
   OpenMP around NTL arithmetic without restoring the NTL context per worker.
2. Accumulate per-thread local coefficient arrays and reduce at the end. Do not
   write shared `NTL::ZZ_pE` values concurrently.
3. Keep single-thread and multi-thread outputs byte-identical.
4. Benchmark both `--threads 1` and `--threads 16`.

Acceptance:

1. Multi-thread results are byte-identical and verifier-accepted.
2. `--threads 16` improves wall-clock time on `m=4` or larger rows after Phase
   1.
3. If `m=3` is too small to benefit, document it as a workload-size threshold
   rather than forcing parallel overhead into small rows.

Phase 5 validation on 2026-05-03:

1. Parallelized WHIR oracle encoding with
   `GRContext::parallel_for_chunks_with_ntl_context(...)`. Each worker restores
   the NTL context and writes only disjoint oracle slots; the Merkle tree,
   transcript schedule, query derivation, and proof serialization remain
   unchanged.
2. Added a regression test that opens the same WHIR commitment with 1 and 4
   OpenMP threads, compares the commitment root and proof messages while
   ignoring timing counters, and verifies both openings.
3. Focused release WHIR CTest passed: 7/7 tests.
4. Baseline `m=3`, `--warmup 1 --reps 3`, produced:
   - `--threads 1`: `serialized_bytes_actual=32360`,
     `prover_total_ms=893.713`,
     `profile_prover_encode_mean_ms=105.660`,
     `profile_prover_fold_mean_ms=30.215`.
   - `--threads 16`: `serialized_bytes_actual=32360`,
     `prover_total_ms=885.876`,
     `profile_prover_encode_mean_ms=105.718`,
     `profile_prover_fold_mean_ms=18.498`.
   The `m=3` row remains a small workload; the encode threshold avoids forcing
   parallel overhead into domains smaller than 128.
5. The `m=4`, `--warmup 0 --reps 1` row produced:
   - `--threads 1`: `serialized_bytes_actual=131636`,
     `prover_total_ms=8052.558`,
     `profile_prover_encode_mean_ms=1538.592`.
   - `--threads 16`: `serialized_bytes_actual=131636`,
     `prover_total_ms=6726.754`,
     `profile_prover_encode_mean_ms=264.098`.

## Recommended Execution Order

1. Phase 0: lock the benchmark and test harness.
2. Phase 1: direct coefficient-space sumcheck. This is the main expected win.
3. Phase 2: cache `eq_B` and constraint factors if Phase 1 profiles still show
   algebraic overhead inside sumcheck.
4. Phase 3: remove duplicated oracle/tree work.
5. Phase 4: verifier query cleanup.
6. Phase 5: parallelize the remaining large loops.

## Commands for Final Acceptance

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 16

ctest --test-dir build-release --output-on-failure \
  -R 'test_whir(_constraint|_folding|_roundtrip|_multiquadratic|_multilinear|_soundness)?$'

./build-release/bench_time --protocol whir_gr_ud \
  --lambda 64 --k-exp 16 --whir-m 3 --whir-bmax 1 --whir-rho0 1/3 \
  --whir-polynomial multiquadratic --hash-profile WHIR_NATIVE \
  --threads 1 --warmup 1 --reps 3 --format text

./build-release/bench_time --protocol whir_gr_ud \
  --lambda 64 --k-exp 16 --whir-m 3 --whir-bmax 1 --whir-rho0 1/3 \
  --whir-polynomial multiquadratic --hash-profile WHIR_NATIVE \
  --threads 16 --warmup 1 --reps 3 --format text

timeout 180s ./build-release/bench_time --protocol whir_gr_ud \
  --lambda 64 --k-exp 16 --whir-m 4 --whir-bmax 1 --whir-rho0 1/3 \
  --whir-polynomial multiquadratic --hash-profile WHIR_NATIVE \
  --threads 1 --warmup 0 --reps 1 --format text
```

Final report should include before/after values for:

1. `prover_total_ms`
2. `profile_prover_interpolate_mean_ms`
3. `profile_prover_encode_mean_ms`
4. `profile_prover_fold_mean_ms`
5. `verify_ms`
6. `profile_verify_query_mean_ms`
7. `profile_verify_algebra_mean_ms`
8. `serialized_bytes_actual`

Final acceptance validation on 2026-05-03:

1. `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` completed.
2. `cmake --build build-release -j 16` completed.
3. Focused release WHIR CTest passed: 7/7 tests.
4. Current `m=3`, `--warmup 1 --reps 3`, `--threads 1`:
   `prover_total_ms=892.556`,
   `profile_prover_interpolate_mean_ms=171.726`,
   `profile_prover_encode_mean_ms=105.370`,
   `profile_prover_fold_mean_ms=30.172`,
   `verify_ms=717.077`,
   `profile_verify_query_mean_ms=131.126`,
   `profile_verify_algebra_mean_ms=533.639`,
   `serialized_bytes_actual=32360`.
5. Current `m=3`, `--warmup 1 --reps 3`, `--threads 16`:
   `prover_total_ms=885.040`,
   `profile_prover_interpolate_mean_ms=172.035`,
   `profile_prover_encode_mean_ms=106.260`,
   `profile_prover_fold_mean_ms=18.134`,
   `verify_ms=720.654`,
   `profile_verify_query_mean_ms=131.720`,
   `profile_verify_algebra_mean_ms=536.740`,
   `serialized_bytes_actual=32360`.
6. Current `m=4`, `--warmup 0 --reps 1`, `--threads 1`:
   `prover_total_ms=8047.204`,
   `profile_prover_interpolate_mean_ms=2035.189`,
   `profile_prover_encode_mean_ms=1541.605`,
   `profile_prover_fold_mean_ms=127.278`,
   `verify_ms=5036.903`,
   `profile_verify_query_mean_ms=718.142`,
   `profile_verify_algebra_mean_ms=4158.964`,
   `serialized_bytes_actual=131636`.
7. Current `m=4`, `--warmup 0 --reps 1`, `--threads 16`:
   `prover_total_ms=6719.762`,
   `profile_prover_interpolate_mean_ms=2040.379`,
   `profile_prover_encode_mean_ms=264.386`,
   `profile_prover_fold_mean_ms=42.989`,
   `verify_ms=5052.538`,
   `profile_verify_query_mean_ms=720.992`,
   `profile_verify_algebra_mean_ms=4170.925`,
   `serialized_bytes_actual=131636`.

## Per-Item Optimization Log on 2026-05-03

### Kept: Hybrid FFT Encode for Single-Thread Proving

Protocol-visible behavior is unchanged: the committed oracle evaluations are
still evaluations of `f(X, X^3, X^9, ...)` over the same WHIR domain, and the
transcript labels, query derivation, Merkle payloads, and proof fields are
unchanged. The implementation uses the existing `rs_encode`/`fft3` path when
OpenMP is configured for one thread, and keeps the previous parallel Horner
encoder when multiple OpenMP threads are available.

Validation:

1. `cmake --build build-release -j 16 --target test_whir_multiquadratic test_whir test_whir_roundtrip bench_time`
   completed.
2. `ctest --test-dir build-release --output-on-failure -R 'test_whir(_multiquadratic|_roundtrip)?$'`
   passed: 3/3 tests.
3. Added a regression test checking `rs_encode(domain,
   f.to_univariate_pow_polynomial(ctx))` against direct `evaluate_pow` on a
   ternary WHIR domain.

Timing comparison:

1. `m=3`, `--threads 1`, `--warmup 1 --reps 3`:
   - before: `prover_total_ms=893.882`,
     `profile_prover_encode_mean_ms=105.633`,
     `verify_ms=717.589`,
     `serialized_bytes_actual=32360`.
   - after: `prover_total_ms=843.342`,
     `profile_prover_encode_mean_ms=48.915`,
     `verify_ms=724.922`,
     `serialized_bytes_actual=32360`.
2. `m=4`, `--threads 1`, `--warmup 0 --reps 1`:
   - before: `prover_total_ms=8047.204`,
     `profile_prover_encode_mean_ms=1541.605`,
     `verify_ms=5036.903`,
     `serialized_bytes_actual=131636`.
   - after: `prover_total_ms=6962.008`,
     `profile_prover_encode_mean_ms=386.815`,
     `verify_ms=5087.367`,
     `serialized_bytes_actual=131636`.
3. `m=4`, `--threads 16`, `--warmup 0 --reps 1`:
   - before: `prover_total_ms=6732.008`,
     `profile_prover_encode_mean_ms=265.883`,
     `verify_ms=5114.210`,
     `serialized_bytes_actual=131636`.
   - after hybrid reruns: `prover_total_ms=6788.970` and `6804.510`,
     `profile_prover_encode_mean_ms=266.617` and `266.901`,
     `serialized_bytes_actual=131636`.

Decision: keep the hybrid form. Pure FFT was rejected for the multi-thread
path because it slightly regressed the `m=4`, `--threads 16` row; the retained
version gives large single-thread encode wins while leaving the multi-thread
encoder on the previous path.

### Rejected: Remove Initial `open` Polynomial/Oracle Deep Copies

Protocol-visible behavior would have stayed unchanged because the change only
replaced local prover copies with references to commit-state data plus owned
storage for later rounds. Timings did not improve enough to keep it:

1. `m=3`, `--threads 1`, `--warmup 1 --reps 3`:
   `prover_total_ms=843.342` before, `847.135` after,
   `serialized_bytes_actual=32360`.
2. `m=4`, `--threads 16`, `--warmup 0 --reps 1`:
   after trial `prover_total_ms=6828.585`,
   `serialized_bytes_actual=131636`, compared with the post-FFT baseline
   band of `6788.970` to `6804.510`.

Decision: reverted and not committed.

### Rejected: Formula-Only WHIR Byte Counting

Protocol-visible behavior would have stayed unchanged because the trial changed
only `serialized_message_bytes`; transcript absorption still used the existing
`Serialize*` routines. Timings were noise-level:

1. `m=3`, `--threads 1`, `--warmup 1 --reps 3`:
   `prover_total_ms=843.342` before, `842.618` after,
   `serialized_bytes_actual=32360`.
2. `m=4`, `--threads 16`, `--warmup 0 --reps 1`:
   after trial `prover_total_ms=6781.770`,
   `serialized_bytes_actual=131636`, within the same noise band as the
   post-FFT baseline.

Decision: reverted and not committed.

### Kept: Verifier `eq_B` Lagrange Basis Cache

Protocol-visible behavior is unchanged: verifier-side constraint restriction
and final `W` evaluation use the same equality-kernel formula over the same
ternary grid, but cache the three Lagrange basis polynomials instead of
rebuilding and inverting their denominators for every `eq_B` call. Sumcheck
checks, Merkle checks, query derivation, transcript labels, and proof fields are
unchanged.

Validation:

1. `cmake --build build-release -j 16 --target test_whir test_whir_roundtrip bench_time`
   completed.
2. `ctest --test-dir build-release --output-on-failure -R 'test_whir(_roundtrip)?$'`
   passed: 2/2 tests.
3. Focused release WHIR CTest passed:
   `ctest --test-dir build-release --output-on-failure -R 'test_whir(_constraint|_folding|_roundtrip|_multiquadratic|_multilinear|_soundness)?$'`
   passed 7/7 tests.

Timing comparison:

1. `m=3`, `--threads 1`, `--warmup 1 --reps 3`:
   - before: `verify_ms=724.922`,
     `profile_verify_algebra_mean_ms=540.454`,
     `serialized_bytes_actual=32360`.
   - after: `verify_ms=245.271`,
     `profile_verify_algebra_mean_ms=57.281`,
     `serialized_bytes_actual=32360`.
2. `m=4`, `--threads 16`, `--warmup 0 --reps 1`:
   - before: `verify_ms=5107.671` to `5228.163`,
     `profile_verify_algebra_mean_ms=4217.522` to `4318.551`,
     `serialized_bytes_actual=131636`.
   - after: `verify_ms=1309.944`,
     `profile_verify_algebra_mean_ms=412.838`,
     `serialized_bytes_actual=131636`.

Decision: keep and commit. This is the largest retained verifier-side win in
the protocol-preserving follow-up batch.

## Non-Goals

1. Do not change WHIR selector semantics in `src/whir/soundness.cpp`.
2. Do not change the transcript schedule or labels in `src/whir/common.cpp`.
3. Do not change proof serialization to make timings look better.
4. Do not start with Merkle/hash tuning; the current `m=3` release row spends
   less than 10 ms in prover Merkle work.
5. Do not present debug-mode speedups as final performance results.
