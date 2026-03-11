# STIR over GR Theorem-Facing Migration TODO

## Scope

- Goal: migrate the current `STIR(9->3)` prototype to a conservative theorem-facing `STIR over Galois rings` path that is consistent with the existing `Z2KSNARK` GR proximity results.
- First landing target: `theorem_gr_conservative`.
- The first theorem-facing landing should use:
  - Teichmuller / exceptional-set sampling for verifier randomness.
  - OOD sampling in a unique-decoding regime.
  - Conservative GR proximity assumptions only.
- Non-goals for the first landing:
  - Johnson / list-decoding based OOD soundness.
  - Claiming full field-paper-equivalent STIR soundness.
  - Redesigning the current public `StirProof` shape.
  - Replacing the fixed `9 -> 3` route with a fully generic STIR family.

## Current Truth To Preserve

- Keep the current prototype path available until theorem mode is stable.
- Do not change the external proof shape in the first migration:
  - `initial_root`
  - per-round `g_root + betas + ans_polynomial + shake_polynomial + queries_to_prev`
  - `queries_to_final + final_polynomial`
- Do not describe theorem mode as paper-complete until:
  - theorem-facing sampler semantics are wired end to end,
  - theorem-mode validation rejects unsupported parameter sets,
  - STIR theorem metadata no longer routes through `engineering_metadata_non_paper`.

## Acceptance Criteria For The First Landing

- `StirParameters` can express both `prototype` and `theorem_gr_conservative` modes.
- In theorem mode:
  - verifier folding challenges are sampled from `T`,
  - verifier comb challenges are sampled from `T`,
  - OOD and shake points are sampled from an explicit exceptional safe complement,
  - all round domains are validated as subsets of `T*`,
  - OOD is treated in a unique-decoding regime,
  - benchmark metadata reports theorem-facing STIR soundness separately from engineering STIR metadata.
- README explicitly distinguishes:
  - prototype STIR,
  - conservative theorem-facing GR-STIR.

## Phase 0: Freeze Theorem Target And Guardrails

- [ ] Add this document to tracking and keep it as the single execution checklist.
- [ ] Create a short proof-note companion file `notes/stir_over_gr_theorem_facing_soundness.md`.
- [ ] In that note, pin the first landing theorem statement:
  - challenge sampling from `T`,
  - OOD in unique-decoding mode,
  - conservative GR proximity window,
  - no Johnson/list-decoding claims.
- [ ] In that note, list the exact external dependencies from `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md` and `mineru-md/Z2KSNARK(Full-Version)/hybrid_auto/Z2KSNARK(Full-Version).md`.
- [ ] In that note, state which STIR field lemmas are replaced by GR variants:
  - GR folding-soundness lemma replacing field `Lemma 4.9`.
  - GR degree-correction/combine lemma replacing field `Lemma 4.13`.
  - GR quotient-by-wrong-values lemma replacing field `Lemma 4.4`.

Suggested commit:

- `docs: add theorem-facing STIR-over-GR migration checklist`

## Phase 1: Add Explicit STIR Mode And Sampler Configuration

Files:

- `include/stir/parameters.hpp`
- `src/stir/parameters.cpp`

Types to add:

- [x] `enum class StirProtocolMode { PrototypeEngineering, TheoremGrConservative };`
- [x] `enum class StirChallengeSampling { AmbientRing, TeichmullerT };`
- [x] `enum class StirOodSamplingMode { PrototypeShiftedCoset, TheoremExceptionalComplementUnique };`

Fields to add to `StirParameters`:

- [x] `StirProtocolMode protocol_mode`
- [x] `StirChallengeSampling challenge_sampling`
- [x] `StirOodSamplingMode ood_sampling`

Functions to update:

- [x] `bool validate(const StirParameters& params)`
- [x] `bool validate(const StirParameters& params, const StirInstance& instance)`
- [x] `std::vector<RoundQueryScheduleMetadata> resolve_query_schedule_metadata(...)`
- [x] `std::vector<std::uint64_t> resolve_query_repetitions(...)`

Implementation notes:

- [x] Preserve the current defaults as `PrototypeEngineering`.
- [x] Add a constructor-free default theorem configuration helper inside `src/stir/parameters.cpp` if repeated initialization becomes noisy.
- [x] In theorem mode, reject configurations that still request ambient-ring challenge sampling or prototype OOD sampling.

Acceptance:

- [x] Existing prototype tests still pass unchanged.
- [x] Theorem mode can be instantiated without changing prover/verifier behavior yet.

Suggested commit:

- `stir: add theorem-facing protocol and sampling modes`

## Phase 2: Add STIR-Specific Theorem-Facing Samplers

Files:

- `include/stir/common.hpp`
- `src/stir/common.cpp`

Functions to add:

- [x] `swgr::algebra::GRElem derive_stir_folding_challenge(swgr::crypto::Transcript&, const swgr::algebra::GRContext&, std::string_view label);`
- [x] `swgr::algebra::GRElem derive_stir_comb_challenge(swgr::crypto::Transcript&, const swgr::algebra::GRContext&, std::string_view label);`
- [x] `bool domain_is_subset_of_teichmuller_units(const Domain& domain);`
- [x] `std::vector<swgr::algebra::GRElem> derive_theorem_ood_points(const Domain& input_domain, const Domain& shift_domain, const Domain& folded_domain, swgr::crypto::Transcript& transcript, std::string_view label_prefix, std::uint64_t sample_count);`
- [x] `swgr::algebra::GRElem derive_theorem_shake_point(const Domain& input_domain, const Domain& shift_domain, const Domain& folded_domain, const std::vector<swgr::algebra::GRElem>& quotient_points, swgr::crypto::Transcript& transcript, std::string_view label_prefix);`

Existing functions to refactor behind mode switches:

- [x] `derive_ood_points(...)`
- [x] `derive_shake_point(...)`

Implementation notes:

- [x] `derive_stir_folding_challenge(...)` must route to `Transcript::challenge_teichmuller(...)`.
- [x] `derive_stir_comb_challenge(...)` must route to `Transcript::challenge_teichmuller(...)`.
- [x] The theorem OOD helper must sample from an explicit safe complement, not from `input_domain.scale_offset(1)` as an implicit engineering candidate domain.
- [x] The theorem shake helper must sample from an explicit safe complement of the current quotient set.
- [x] Keep the current prototype helpers as separate code paths to avoid breaking current benchmarks during migration.

Acceptance:

- [x] New helpers compile without changing current prover/verifier outputs in prototype mode.
- [x] Unit tests can directly assert that theorem fold/comb challenges lie in `T`.

Suggested commit:

- `stir: add theorem-facing Teichmuller and exceptional samplers`

## Phase 3: Wire Theorem Samplers Into Prover And Verifier

Files:

- `src/stir/prover.cpp`
- `src/stir/verifier.cpp`

Functions to update:

- [ ] `StirProver::prove(...)`
- [ ] `StirVerifier::verify(...)`

Exact call sites to replace with mode-aware routing:

- [ ] Initial `current_folding_alpha` derivation in `StirProver::prove(...)`
- [ ] Per-round `comb_randomness` derivation in `StirProver::prove(...)`
- [ ] Per-round `next_folding_alpha` derivation in `StirProver::prove(...)`
- [ ] Per-round OOD derivation in `StirProver::prove(...)`
- [ ] Per-round shake derivation in `StirProver::prove(...)`
- [ ] Initial `state.folding_alpha` derivation in `StirVerifier::verify(...)`
- [ ] Per-round `comb_randomness` derivation in `StirVerifier::verify(...)`
- [ ] Per-round `next_folding_alpha` derivation in `StirVerifier::verify(...)`
- [ ] Per-round OOD derivation in `StirVerifier::verify(...)`
- [ ] Per-round shake derivation in `StirVerifier::verify(...)`

Implementation notes:

- [ ] Do not change `RoundLabel(...)` strings in the first landing.
- [ ] Do not change `StirProof` serialization in the first landing.
- [ ] Keep proof shape identical; only change verifier randomness semantics when `protocol_mode == TheoremGrConservative`.

Acceptance:

- [ ] Honest theorem-mode prover/verifier round-trips succeed.
- [ ] Prototype mode still reproduces the pre-migration behavior.

Suggested commit:

- `stir: route theorem mode through Teichmuller and exceptional sampling`

## Phase 4: Add Theorem-Mode Validation And Stop Rules

Files:

- `include/stir/common.hpp`
- `src/stir/common.cpp`
- `src/stir/parameters.cpp`

Helpers to add:

- [ ] `bool theorem_ood_pool_has_capacity(const Domain& input_domain, const Domain& shift_domain, const Domain& folded_domain, std::uint64_t required_points);`
- [ ] `bool theorem_shake_pool_has_capacity(const Domain& input_domain, const Domain& shift_domain, const Domain& folded_domain, std::span<const swgr::algebra::GRElem> quotient_points);`

Functions to update:

- [ ] `validate(const StirParameters& params, const StirInstance& instance)`

Theorem-mode checks to enforce:

- [ ] `instance.domain` is a subset of `T*`.
- [ ] Every derived `folded_domain` is a subset of `T*`.
- [ ] Every derived `shift_domain` is a subset of `T*`.
- [ ] `shift_domain` and `folded_domain` satisfy unit-difference requirements.
- [ ] The theorem OOD candidate pool is large enough for `ood_samples`.
- [ ] The theorem shake candidate pool is non-empty after excluding the quotient set.
- [ ] `ood_samples + effective_query_count <= next_degree_bound + 1`.

Stop rule:

- [ ] If theorem-mode validation fails, reject the instance; do not silently fall back to prototype semantics.

Acceptance:

- [ ] Invalid theorem-mode instances fail early in `validate(...)`.
- [ ] Existing prototype validation remains unchanged.

Suggested commit:

- `stir: enforce theorem-mode domain and sampling guardrails`

## Phase 5: Add Conservative Theorem Soundness Module

Files to add:

- `include/stir/soundness.hpp`
- `src/stir/soundness.cpp`

Build-system changes:

- [ ] Add `src/stir/soundness.cpp` to `STIR_OVER_GR_SOURCES` in `CMakeLists.txt`.

Types to add:

- [ ] `enum class StirTheoremSoundnessFlavor { GrConservativeUniqueOod };`
- [ ] `struct StirRoundTheoremSoundnessTerm`
- [ ] `struct StirTheoremSoundnessAnalysis`

Suggested fields for `StirRoundTheoremSoundnessTerm`:

- [ ] `round_index`
- [ ] `epsilon_out`
- [ ] `epsilon_shift`
- [ ] `degree_bound`
- [ ] `domain_size`
- [ ] `effective_query_count`
- [ ] `notes`

Suggested fields for `StirTheoremSoundnessAnalysis`:

- [ ] `flavor`
- [ ] `feasible`
- [ ] `epsilon_fold`
- [ ] `epsilon_fin`
- [ ] `rounds`
- [ ] `effective_security_bits`
- [ ] `proximity_gap_model`
- [ ] `ood_model`
- [ ] `assumptions`

Functions to add:

- [ ] `StirTheoremSoundnessAnalysis analyze_theorem_soundness(const StirParameters&, const StirInstance&);`
- [ ] Internal helper for fold term analysis.
- [ ] Internal helper for OOD term analysis.
- [ ] Internal helper for shift term analysis.
- [ ] Internal helper for final-query term analysis.

First-landing theorem assumptions to encode:

- [ ] `epsilon_out = 0` in theorem mode because OOD runs in a unique-decoding regime.
- [ ] `epsilon_fold` uses a conservative GR folding-soundness bound derived from existing Z2KSNARK proximity results.
- [ ] `epsilon_shift` is decomposed into:
  - `(1 - delta) ^ t` shift-hit term,
  - conservative GR degree-correction term,
  - conservative GR folding term.
- [ ] Any missing closed-form constants must be surfaced in `assumptions` and `notes`; do not silently invent paper-equivalent constants.

Acceptance:

- [ ] The analysis object can be computed from the live `StirParameters` and `StirInstance`.
- [ ] Theorem mode can report `feasible=false` for unsupported parameter sets instead of producing misleading metadata.

Suggested commit:

- `stir: add conservative theorem-facing soundness analysis`

## Phase 6: Bench And CLI Metadata

Files:

- `bench/bench_time.cpp`
- `CMakeLists.txt` if a dedicated STIR soundness test binary is added

Types to add in `bench/bench_time.cpp`:

- [ ] `enum class StirBenchSoundnessMode { Engineering, TheoremGrConservative };`

Functions to add or update:

- [ ] Parse a new CLI flag `--stir-soundness-mode engineering|theorem_gr_conservative`.
- [ ] Keep existing engineering STIR metadata path as the default until theorem mode is explicitly requested.
- [ ] Add `FillStirTheoremSoundnessMetadata(...)`.
- [ ] Update `RunStirBench(...)` wiring to call `analyze_theorem_soundness(...)` when theorem mode is selected.

Metadata requirements:

- [ ] `soundness_mode = theorem_gr_conservative`
- [ ] `soundness_model = epsilon_rbr_stir_gr_conservative_unique_ood`
- [ ] `soundness_scope = theorem_gr_conservative_existing_z2ksnark_results`
- [ ] `soundness_notes` must mention:
  - unique-decoding OOD
  - Teichmuller challenge sampling
  - conservative GR proximity assumptions

Acceptance:

- [ ] `bench_time` can print both engineering and theorem-facing STIR metadata.
- [ ] No theorem-facing metadata path uses `engineering_metadata_non_paper`.

Suggested commit:

- `bench: expose conservative theorem-facing STIR soundness metadata`

## Phase 7: Tests

Files:

- `tests/test_stir.cpp`
- `tests/test_stir_soundness.cpp` if split coverage becomes cleaner
- `CMakeLists.txt`

Tests to add:

- [ ] Prototype regression test: current prototype path still verifies honest proofs.
- [ ] Theorem sampler test: fold and comb challenges are sampled from `T`.
- [ ] Theorem OOD test: OOD points lie in the explicit exceptional safe complement.
- [ ] Theorem shake test: shake point avoids the quotient set and respects unit-difference constraints.
- [ ] Theorem validator test: non-`T*` domains are rejected.
- [ ] Theorem validator test: insufficient OOD candidate pool is rejected.
- [ ] Honest theorem-mode prove/verify test.
- [ ] Theorem soundness analysis smoke test.
- [ ] Bench-metadata test if a small parser test is practical.

Build-system changes:

- [ ] Add a dedicated `test_stir_soundness` executable in `CMakeLists.txt` if the soundness module becomes too large for `test_stir.cpp`.

Acceptance:

- [ ] `ctest -R 'test_stir|test_stir_soundness'` passes.
- [ ] Honest theorem-mode behavior is covered by a deterministic unit test.

Suggested commit:

- `test: cover theorem-facing STIR sampling, validation, and soundness metadata`

## Phase 8: README And Tracking Docs

Files:

- `README.md`
- `stir_over_gr_theorem_facing_todo.md`
- `notes/stir_over_gr_theorem_facing_soundness.md`

README updates:

- [ ] Replace the current STIR-only wording with an explicit split:
  - prototype fixed-parameter STIR mode
  - theorem-facing conservative GR-STIR mode
- [ ] Add a short theorem-facing STIR contract section parallel to the FRI contract section.
- [ ] State clearly that the first theorem-facing STIR landing is conservative and does not claim Johnson/list-decoding alignment.

Tracking updates:

- [ ] Keep this file updated after each commit.
- [ ] Mark any theorem item that remains assumption-only.
- [ ] Add a short changelog section at the bottom once implementation starts.

Acceptance:

- [ ] README does not overclaim paper equivalence.
- [ ] README is consistent with benchmark metadata and live code paths.

Suggested commit:

- `docs: document conservative theorem-facing STIR-over-GR mode`

## Deferred Follow-Up: Stronger `(1-rho)/2` STIR Mode

- [ ] Add `TheoremGrHalfGap` only after the exact size guard and proof-note constants are written down.
- [ ] Add an explicit validator helper for the `|L|^2 << |T|` style regime before enabling the stronger mode.
- [ ] Do not merge a half-gap code path that only changes metadata strings without enforcing the parameter guard in code.

Suggested future commit:

- `stir: add guarded half-gap theorem mode`

## Suggested Commit Series

- `stir: add theorem-facing protocol and sampling modes`
- `stir: add theorem-facing Teichmuller and exceptional samplers`
- `stir: route theorem mode through Teichmuller and exceptional sampling`
- `stir: enforce theorem-mode domain and sampling guardrails`
- `stir: add conservative theorem-facing soundness analysis`
- `bench: expose conservative theorem-facing STIR soundness metadata`
- `test: cover theorem-facing STIR sampling, validation, and soundness metadata`
- `docs: document conservative theorem-facing STIR-over-GR mode`

## Explicit Out-Of-Scope Cleanups

- `StirProof::stats` serialization mismatch is a separate maintenance fix; do not mix it into the theorem migration unless it directly blocks theorem-mode tests.
- Genericizing beyond the fixed `9 -> 3` route is a later protocol-expansion task, not a prerequisite for the conservative theorem-facing landing.
