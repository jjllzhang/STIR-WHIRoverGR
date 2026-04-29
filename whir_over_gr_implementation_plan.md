# WHIR over Galois Rings Implementation Plan

## Conclusion

The current repository is in a good enough state to implement a conservative
WHIR-style PCS over `GR(2^s, r)`, but the implementable target is not the full
finite-field WHIR paper with Johnson/list-decoding and out-of-domain uniqueness.

The feasible first target is the protocol in `whir_gr2k_pcs.pdf`:

- hash-based PCS over `R = GR(2^s, r)`;
- ternary domains inside `T^*` because `|T^*| = 2^r - 1` is odd;
- multi-quadratic polynomials, i.e. individual degree `< 3`;
- unique-decoding soundness below `(1 - rho) / 2`;
- no WHIR list-decoding OOD uniqueness layer;
- BCS-style Merkle commitments and Fiat-Shamir transcript.

This is implementable because the repo already has the required GR substrate:

- `GRContext`, serialization, units, inverses, Teichmuller elements, and
  subgroup generators in `include/algebra/*` and `src/algebra/*`;
- `Domain::pow_map(3)` and Teichmuller subgroup/coset domains in
  `include/domain.hpp` / `src/domain.cpp`;
- radix-3 FFT, interpolation, and folding helpers in `include/poly_utils/*`;
- BLAKE3 transcript, Teichmuller challenge sampling, Merkle trees, and pruned
  multiproof support in `include/crypto/*`;
- working FRI and STIR proof surfaces that already exercise these primitives.

The main missing part is WHIR itself. `include/whir/*` currently defines only a
minimal skeleton, and `src/whir/prover.cpp` / `src/whir/verifier.cpp` still throw
`throw_unimplemented(...)`.

## Scope Boundary

Implement this first:

- `Setup / Commit / Open / Verify` for a single opening of a multi-quadratic
  polynomial over `GR(2^s, r)`;
- public parameters generated from `(lambda, s, m, bmax, rho0, theta)`;
- one commitment root for the initial table and one Merkle root per WHIR layer;
- ternary sumcheck over `B = {1, omega, omega^2}`;
- shift queries using virtual repeated ternary folds of the current oracle;
- final constant-oracle check;
- proof-byte accounting from the actual serialized proof object;
- `bench_time` support for a `whir_gr_ud` protocol row.

Do not claim this first version implements:

- WHIR-JB / WHIR-CB list-decoding modes from `WHIR.pdf`;
- the Rust reference implementation's full finite-field genericity;
- arbitrary `GR(p^k, r)` for odd `p`;
- ZK WHIR;
- production cryptographic assurance.

## Paper-to-Code Mapping

### `WHIR.pdf`

The finite-field WHIR paper supplies the high-level structure:

- constrained Reed-Solomon codes;
- folding plus sumcheck;
- shifted oracle messages;
- random linear combination of residual and shift constraints;
- final low-dimensional oracle check;
- BCS/Merkle compilation in the implementation section.

However, its main protocol is binary/multilinear over finite fields and its
stronger parameter modes rely on list-decoding assumptions. Those pieces should
not be copied literally into this GR implementation.

### `whir_gr2k_pcs.pdf`

This is the implementation spec to follow for the first GR version:

- Section 3: `R = GR(2^s, r)`, Teichmuller set `T`, exceptional-set unit
  differences, ternary domains.
- Section 3.3: multi-quadratic polynomial representation and the univariate
  `Pow_m(x)` encoding.
- Section 4: CRS constraint form. For PCS opening, use
  `W_0(Z, X) = Z * eq_B(z, X)` and `sigma_0 = y`.
- Section 5: one-step and repeated ternary folding.
- Section 6: WHIR-over-GR IOPP layer:
  sumcheck, shifted folded oracle, shift queries, random linear combination.
- Section 7: hash-based PCS algorithms `Setup`, `Commit`, `Open`, `Verify`.
- Section 8: unique-decoding soundness bound.
- Section 9: parameter selection from `lambda`.
- Section 10: implementation notes, especially unit denominators and virtual
  fold query implementation.

### `$HOME/whir`

The Rust reference implementation is useful for architecture, not direct porting:

- `src/protocols/whir/config.rs` maps protocol parameters to per-round configs.
- `src/protocols/whir/prover.rs` and `verifier.rs` show the transcript order:
  commit, PoW, open previous witness, combine constraints, sumcheck, final
  vector.
- `src/protocols/irs_commit.rs` shows the matrix/IRS commitment organization
  and the way in-domain samples are opened.
- `src/protocols/sumcheck.rs` is a compact reference for transcript-driven
  sumcheck.

It is finite-field Rust over `ark_ff::FftField` with power-of-two domains and
binary folding, so it cannot be copied into this C++ GR code path.

## Design Decisions

1. Target only `p = 2` for the first WHIR-over-GR release.

   The GR PCS note uses `T^*` of order `2^r - 1`, ternary subgroups, and
   multi-quadratic encodings. The repo's `GRContext` can represent other prime
   powers, but soundness and parameter selection should reject non-`p=2` WHIR
   params until a separate proof/spec exists.

2. Use a dedicated multi-quadratic polynomial representation.

   Existing `poly_utils::Polynomial` is univariate. WHIR needs operations on
   coefficients indexed by base-3 variable tuples:

   - evaluate `F(z_0, ..., z_{m-1})`;
   - evaluate `F(Pow_m(x))`;
   - restrict a prefix of variables to `alpha_i`;
   - compute `sum_{x in B^m} F(x) * A(x)` for sumcheck;
   - encode the restricted polynomial on `H_i`.

   Store coefficients as a flat vector of length up to `3^m`, with base-3 index
   helpers. Provide conversion to a univariate `Polynomial` only when encoding
   `F(Pow_m(x))`.

3. Represent WHIR constraints as linear combinations of equality polynomials.

   In the PCS protocol, every constraint has the form:

   `W_i(Z, X) = Z * A_i(X)`

   where `A_i(X)` is a sum of weighted `eq_B(point, X)` terms. This stays closed
   under layer restriction and shift-query updates:

   - start with `A_0 = eq_B(z, X)`;
   - after sumcheck challenge `alpha_i`, restrict all existing terms;
   - add `sum_j gamma_i^j * eq_B(z_{i,j}, X)`;
   - update `sigma_{i+1} = tau_i + sum_j gamma_i^j * y_{i,j}`.

   This avoids building a generic symbolic polynomial engine.

4. Implement repeated ternary folds explicitly.

   Existing `fold_table_k(domain, evals, k, alpha)` is a one-shot degree-`< k`
   interpolation at one scalar. WHIR layer width `b` uses a vector
   `alpha = (alpha_0, ..., alpha_{b-1})` and repeated ternary folds. Add helpers
   that fold by `3` repeatedly, both for whole tables and for sparse virtual
   query fibers.

5. Use `Domain::pow_map(3)` for WHIR shifted oracle domains.

   The GR WHIR note sets `H_{i+1} = H_i^(3)`. This is not the same as STIR's
   `scale_offset(3)` shifted coset path. WHIR code should use `pow_map(3)`.

6. Keep first implementation correctness-first.

   Start with simple deterministic tables, direct enumeration for small
   sumcheck domains, and singleton Merkle leaves. Add batching and tensorized
   speedups only after roundtrip and tamper tests are stable.

## New and Modified Files

Primary WHIR files:

- `include/whir/common.hpp`
- `include/whir/parameters.hpp`
- `include/whir/prover.hpp`
- `include/whir/verifier.hpp`
- `include/whir/multiquadratic.hpp`
- `include/whir/constraint.hpp`
- `include/whir/folding.hpp`
- `include/whir/soundness.hpp`
- `src/whir/common.cpp`
- `src/whir/parameters.cpp`
- `src/whir/prover.cpp`
- `src/whir/verifier.cpp`
- `src/whir/multiquadratic.cpp`
- `src/whir/constraint.cpp`
- `src/whir/folding.cpp`
- `src/whir/soundness.cpp`

Tests and bench:

- `tests/test_whir_multiquadratic.cpp`
- `tests/test_whir_constraint.cpp`
- `tests/test_whir_folding.cpp`
- `tests/test_whir.cpp`
- `bench/bench_time.cpp`
- `CMakeLists.txt`
- `README.md`

## Data Model

### Public Parameters

Add:

```cpp
struct WhirPublicParameters {
  std::shared_ptr<const algebra::GRContext> ctx;
  Domain initial_domain;
  std::uint64_t m = 0;
  std::vector<std::uint64_t> layer_widths;
  std::vector<std::uint64_t> shift_repetitions;
  std::uint64_t final_repetitions = 0;
  std::vector<std::uint64_t> degree_bounds;
  std::vector<long double> deltas;
  algebra::GRElem omega;
  std::array<algebra::GRElem, 3> ternary_grid;
  std::uint64_t lambda_target = 128;
  HashProfile hash_profile = HashProfile::WHIR_NATIVE;
};
```

Validation must check:

- `ctx->config().p == 2`;
- `initial_domain` is a Teichmuller unit subgroup;
- `initial_domain.size()` is divisible by every needed `3^(i + b_i)`;
- every live rate `rho_i = 3^{m_i} / n_i` is `< 1`;
- every `delta_i` is in `(0, (1 - rho_i) / 2)`;
- `T` has enough elements for degree interpolation and FS sampling;
- each selected domain size divides `2^r - 1`.

### Polynomial

Add:

```cpp
class MultiQuadraticPolynomial {
 public:
  MultiQuadraticPolynomial(std::uint64_t variable_count,
                           std::vector<algebra::GRElem> coefficients);
  std::uint64_t variable_count() const;
  const std::vector<algebra::GRElem>& coefficients() const;

  algebra::GRElem evaluate(const algebra::GRContext& ctx,
                           std::span<const algebra::GRElem> point) const;
  algebra::GRElem evaluate_pow(const algebra::GRContext& ctx,
                               const algebra::GRElem& x) const;
  MultiQuadraticPolynomial restrict_prefix(
      const algebra::GRContext& ctx,
      std::span<const algebra::GRElem> alphas) const;
  poly_utils::Polynomial to_univariate_pow_polynomial(
      const algebra::GRContext& ctx) const;
};
```

### Constraint

Add:

```cpp
struct EqTerm {
  algebra::GRElem weight;
  std::vector<algebra::GRElem> point;
};

class WhirConstraint {
 public:
  algebra::GRElem evaluate_A(const algebra::GRContext& ctx,
                             std::span<const algebra::GRElem> x) const;
  algebra::GRElem evaluate_W(const algebra::GRContext& ctx,
                             const algebra::GRElem& z,
                             std::span<const algebra::GRElem> x) const;
  WhirConstraint restrict_prefix(const algebra::GRContext& ctx,
                                 std::span<const algebra::GRElem> alphas) const;
  void add_shift_term(algebra::GRElem weight,
                      std::vector<algebra::GRElem> point);
};
```

### Proof Objects

Replace the current skeleton with:

```cpp
struct WhirCommitment {
  WhirPublicParameters pp;
  std::vector<std::uint8_t> root;
  ProofStatistics stats;
};

struct WhirCommitmentState {
  MultiQuadraticPolynomial polynomial;
  std::vector<algebra::GRElem> initial_oracle;
  crypto::MerkleTree initial_tree;
};

struct WhirSumcheckPolynomial {
  std::vector<algebra::GRElem> coefficients; // degree <= D_i
};

struct WhirRoundProof {
  std::vector<WhirSumcheckPolynomial> sumcheck_polynomials;
  std::vector<std::uint8_t> g_root;
  crypto::MerkleProof virtual_fold_openings;
};

struct WhirProof {
  std::vector<WhirRoundProof> rounds;
  algebra::GRElem final_constant;
  crypto::MerkleProof final_openings;
  ProofStatistics stats;
};

struct WhirOpening {
  algebra::GRElem value;
  WhirProof proof;
};
```

If storing `crypto::MerkleTree` directly in `WhirCommitmentState` becomes awkward
because it is not default-constructible, use `std::unique_ptr<crypto::MerkleTree>`
or store the initial oracle and rebuild in `Open`.

## Implementation Phases

### Phase 0: Freeze protocol constants and transcript labels

Deliverables:

- Define labels for transcript absorption:
  - `"whir.pp"`
  - `"whir.commitment"`
  - `"whir.open.point"`
  - `"whir.open.value"`
  - `"whir.sumcheck.poly:i:j"`
  - `"whir.alpha:i:j"`
  - `"whir.g_root:i"`
  - `"whir.shift:i:j"`
  - `"whir.gamma:i"`
  - `"whir.final.constant"`
  - `"whir.final.query:j"`
- Decide exact proof serialization order and document it in `src/whir/common.cpp`.
- Add `serialized_message_bytes(ctx, WhirProof)` and
  `serialized_message_bytes(ctx, WhirOpening)`.

Acceptance:

- Serialization byte count is deterministic.
- Empty/invalid proof shapes reject in tests.

### Phase 1: Multi-quadratic algebra

Implement:

- base-3 index encode/decode helpers;
- `pow3_checked(m)` with overflow rejection;
- `Pow_m(x) = (x, x^3, x^9, ...)`;
- `MultiQuadraticPolynomial::evaluate`;
- `evaluate_pow`;
- `restrict_prefix`;
- `to_univariate_pow_polynomial`;
- random/sample constructors in tests only.

Tests:

- `evaluate_pow(x)` equals univariate polynomial evaluation at `x`;
- `restrict_prefix(alpha).evaluate(tail)` equals original evaluation at
  `(alpha, tail)`;
- multilinear polynomials embedded with zero quadratic coefficients behave as
  multi-quadratic polynomials;
- invalid coefficient length and overflow reject.

Command:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure -R test_whir_multiquadratic
```

### Phase 2: Equality constraints and ternary sumcheck

Implement:

- construction of `B = {1, omega, omega^2}`;
- Lagrange basis `L_a(X)` on `B`;
- `eq_B(z, x)`;
- `WhirConstraint` as weighted equality terms;
- prefix restriction for constraint terms;
- honest sumcheck polynomial generation for one variable:
  `h(T) = sum_{b in B^remaining} F(prefix, T, b) * A(prefix, T, b)`;
- degree-`<= 4` interpolation of `h(T)` from five fixed Teichmuller points.

Initial implementation may enumerate `B^remaining`; later optimize by tensor
dynamic programming.

Verifier helpers:

- evaluate a received univariate sumcheck polynomial;
- check degree bound `D_i`;
- check `sum_{a in B} h(a) == current_sigma`;
- update `current_sigma = h(alpha)` after each sumcheck round.

Tests:

- `sum_{x in B^m} F(x) * eq_B(z, x) == F(z)`;
- honest sumcheck identities pass for `m = 1, 2, 3`;
- tampered coefficient fails;
- declared degree above `D_i` fails;
- interpolation points have pairwise unit differences.

### Phase 3: Repeated ternary fold helpers

Implement:

- `repeated_ternary_fold_table(domain, evals, alphas)`:
  repeatedly call the existing `fold_table_k(..., 3, alpha_j)`;
- `virtual_fold_query_indices(domain_size, b, child_index)`:
  returns all `3^b` parent indices above a point in `H_i^(3^b)`;
- `evaluate_repeated_ternary_fold_from_values(points, values, alphas)`:
  recursively group triples and call `fold_eval_k` with one `alpha_j` per level;
- a sparse helper that decodes Merkle leaf payloads and computes
  `Fold_i^{(b)}(f_i; alpha_i)(s)`.

Do not use `fold_table_k(..., 3^b, single_alpha)` for WHIR layer folding.

Tests:

- sparse virtual query equals full repeated table folding for `b = 1, 2, 3`;
- parent indices match `Domain::pow_map(3^b)` fibers;
- non-unit denominator cases reject;
- query deduplication remains deterministic.

### Phase 4: Parameter selection and soundness metadata

Implement `include/whir/soundness.hpp` and `src/whir/soundness.cpp`.

Inputs:

- `lambda`;
- `s`;
- `m`;
- `bmax`;
- `rho0`;
- `theta`;
- optional max `r` / max domain size guard for benchmarks.

Compute:

- layer widths `b_i = min(bmax, m_i)`;
- `M = ceil(m / bmax)`;
- an odd `n0 >= ceil(3^m / rho0)` with enough 3-adic divisibility for all
  `H_{i+b_i}`;
- `rdom = ord_n0(2)`;
- `rho_i = 3^{m_i} / n_i`;
- `delta_i = (1 - theta) * (1 - rho_i) / 2`;
- repetitions `t_i = ceil(Lambda / -log2(1 - delta_i))`;
- `Afold_i = sum_{j=0}^{b_i-1} 2 * (n_i / 3^{j+1})^2`;
- `Balg = sum_i (Afold_i + b_i * D_i + t_i + 1)`;
- `rsec = ceil(log2(2^{lambda+1} * Balg))`;
- `r = rdom * ceil(rsec / rdom)`.

Output:

- public params;
- feasibility flag;
- effective security bits;
- explanatory notes when infeasible.

Tests:

- invalid rates reject;
- selected `n0` divides `2^r - 1`;
- all required domains divide through the round chain;
- algebraic error bound is below `2^-lambda`;
- small smoke params are accepted quickly.

### Phase 5: Commit path

Implement:

```cpp
WhirCommitment WhirProver::commit(
    const WhirPublicParameters& pp,
    const MultiQuadraticPolynomial& polynomial,
    WhirCommitmentState* state) const;
```

Steps:

1. Validate `pp` and polynomial variable count.
2. Convert to univariate `P_F(X)` or evaluate `F(Pow_m(x))` directly.
3. Encode on `H_0`.
4. Merkle-commit with singleton leaves first.
5. Fill commitment stats.

Tests:

- root is stable across repeated commits;
- tampering one table entry changes the root;
- committed polynomial with wrong `m` rejects;
- unsupported `p != 2` rejects.

### Phase 6: Open path

Implement:

```cpp
WhirOpening WhirProver::open(
    const WhirCommitment& commitment,
    const WhirCommitmentState& state,
    std::span<const algebra::GRElem> z) const;
```

Layer loop:

1. Absorb `(pp, com, z, y)` into transcript.
2. Initialize `A_0 = eq_B(z, X)` and `sigma_0 = y`.
3. For each layer `i`:
   - compute and append `b_i` sumcheck polynomials;
   - derive `alpha_i` from `T`;
   - restrict the polynomial by `alpha_i`;
   - encode restricted polynomial on `H_{i+1} = H_i.pow_map(3)`;
   - Merkle-commit `g_i` and absorb `root_i`;
   - derive shift points in `H_i.pow_map(3^{b_i})`;
   - compute virtual fold values from the current oracle table;
   - open the current Merkle tree at all required parent indices;
   - derive `gamma_i`;
   - update the constraint and `sigma`;
   - set current oracle/root/tree to `g_i`.
4. Emit final constant `c`.
5. Derive final query points in `H_M`.
6. Add final Merkle openings.
7. Fill proof stats.

Important implementation detail:

- For `b_i > 1`, the new oracle is encoded on `H_i.pow_map(3)`, while shift
  query points are sampled from `H_i.pow_map(3^{b_i})`.

### Phase 7: Verify path

Implement:

```cpp
bool WhirVerifier::verify(
    const WhirCommitment& commitment,
    std::span<const algebra::GRElem> z,
    const WhirOpening& opening,
    ProofStatistics* stats = nullptr) const;
```

Verification loop:

1. Validate params, commitment, and proof shape.
2. Recompute transcript from `(pp, com, z, y)`.
3. Rebuild `A_0` and `sigma_0`.
4. For each layer:
   - read sumcheck polynomials;
   - check degree bounds and ternary sumcheck identities;
   - derive the same `alpha_i`;
   - absorb `g_root`;
   - derive shift points and `gamma_i`;
   - verify Merkle openings against current root;
   - compute virtual fold answers from opened values;
   - update constraint and `sigma`;
   - set current root to `g_root` and current domain to `H_i.pow_map(3)`.
5. Check `A_M(empty) * c == sigma_M`.
6. Verify final Merkle openings and check all opened values equal `c`.

Tamper tests:

- wrong claimed value rejects;
- changed sumcheck coefficient rejects;
- changed `g_root` rejects;
- changed virtual-fold Merkle payload rejects;
- changed final constant rejects;
- changed final Merkle payload rejects;
- replay with different `z` rejects.

### Phase 8: Benchmark integration

Extend `bench/bench_time.cpp`:

- add protocol name `whir_gr_ud`;
- add options:
  - `--whir-m`;
  - `--whir-bmax`;
  - `--whir-rho0`;
  - `--whir-theta`;
  - `--whir-repetitions` override for debug only;
- keep existing `(p, k_exp, r, n, d)` options for FRI/STIR untouched;
- report:
  - `soundness_mode=theorem_whir_gr_unique_decoding`;
  - `soundness_model=epsilon_iopp_whir_gr_unique_decoding`;
  - `soundness_scope=whir_gr2k_pcs_unique_decoding`;
  - `effective_security_bits`;
  - `serialized_bytes_actual`;
  - prover/verifier timing buckets.

Initial benchmark command:

```bash
./build/bench_time \
  --protocol whir_gr_ud \
  --lambda 64 \
  --whir-m 3 \
  --whir-bmax 1 \
  --whir-rho0 1/3 \
  --warmup 0 \
  --reps 1 \
  --format text
```

Release-style command after smoke is stable:

```bash
OMP_NUM_THREADS=1 ./build-release/bench_time \
  --protocol whir_gr_ud \
  --lambda 128 \
  --whir-m 4 \
  --whir-bmax 1 \
  --whir-rho0 1/3 \
  --threads 1 \
  --warmup 1 \
  --reps 3 \
  --format csv
```

The exact larger benchmark targets should be chosen only after Phase 4 reports
reasonable `r`, `n0`, and proof/query sizes.

### Phase 9: Documentation and cleanup

Update:

- `README.md` Current Scope and Current Limits;
- benchmark notes;
- CMake target list;
- optional result artifact names.

README wording must stay explicit:

- "WHIR-over-GR unique-decoding PCS prototype is implemented";
- not "full WHIR";
- not "Johnson/list-decoding WHIR";
- not "production-ready cryptographic library".

## Validation Matrix

Development validation:

```bash
cmake -S . -B build -DSWGR_BUILD_TESTS=ON -DSWGR_BUILD_BENCH=ON
cmake --build build -j
ctest --test-dir build --output-on-failure -R \
  'test_gr_basic|test_domain|test_fft3|test_folding|test_crypto|test_whir'
```

Full validation:

```bash
ctest --test-dir build --output-on-failure
```

Release validation:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DSWGR_BUILD_TESTS=ON \
  -DSWGR_BUILD_BENCH=ON
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

Fresh-process stability:

```bash
for i in $(seq 1 8); do ./build/test_whir || exit 1; done
```

Benchmark smoke:

```bash
./build/bench_time --protocol whir_gr_ud --lambda 64 \
  --whir-m 3 --whir-bmax 1 --whir-rho0 1/3 \
  --warmup 0 --reps 1 --format text
```

## Risks and Mitigations

1. Parameter explosion for 128-bit targets.

   The GR note's conservative bound can force large `r`. Mitigation: implement
   the parameter selector before the PCS path is considered complete, and use
   small `lambda=32/64` smoke cases first.

2. Confusing STIR shift domains with WHIR domains.

   STIR uses `scale_offset(3)`. WHIR-over-GR uses `pow_map(3)` for `H_{i+1}`.
   Tests must assert the exact domain chain.

3. Accidentally implementing one-shot `3^b` folding instead of repeated ternary
   folding.

   Add direct tests comparing sparse repeated folding to full repeated table
   folding for `b=2`.

4. Sumcheck prover cost from direct enumeration.

   Accept direct enumeration for the first correctness version. Add a separate
   optimization task for tensorized sumcheck once proof semantics are stable.

5. Non-unit denominators.

   All interpolation grids and sampled points must be in Teichmuller exceptional
   sets. Keep verifier-side unit checks, and reject any failed denominator before
   calling `Inv`.

6. Ambiguous proof-size accounting.

   Count only serialized bytes of the actual `WhirOpening` prover reply unless
   a benchmark explicitly asks for commitment plus opening.

## Completion Criteria

The implementation should be considered complete for the first WHIR-over-GR
milestone only when all of the following hold:

- WHIR skeleton no longer throws `throw_unimplemented`;
- `Setup / Commit / Open / Verify` exist and are tested;
- honest openings pass for `m=1,2,3` and at least one `b=2` case;
- all planned tamper tests reject;
- `test_whir` passes in repeated fresh processes;
- `bench_time --protocol whir_gr_ud` emits timing and serialized-byte rows;
- README states the exact unique-decoding scope and exclusions;
- no claim is made for finite-field WHIR list-decoding modes.
