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

## Non-Goals

1. Do not change WHIR selector semantics in `src/whir/soundness.cpp`.
2. Do not change the transcript schedule or labels in `src/whir/common.cpp`.
3. Do not change proof serialization to make timings look better.
4. Do not start with Merkle/hash tuning; the current `m=3` release row spends
   less than 10 ms in prover Merkle work.
5. Do not present debug-mode speedups as final performance results.
