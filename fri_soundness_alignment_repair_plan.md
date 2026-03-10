# FRI Soundness Alignment Repair Plan

Date: 2026-03-10

Owner: Codex + user follow-up execution

Status: Phase 3 landed; Phase 4 pending

## Goal

This plan tracks the three gaps that originally separated the repository's FRI implementation from the Z2KSNARK paper/full-version soundness story:

1. FRI folding challenges `beta_i` are sampled from the full ring instead of from the paper's exceptional set `T`.
2. FRI soundness parameterization is driven by repo-local per-round query schedules and engineering heuristics instead of the paper's repetition parameter `m`.
3. The public PCS proof/opening surface is still a paper-aligned subset, not a full `pi_FRICom` implementation with theorem-facing proof semantics.

The output of this plan should be a repo state where:

- theorem-facing FRI PCS code follows the paper/full-version protocol contract closely enough to support a serious line-by-line audit,
- benchmark and CLI surfaces stop presenting engineering heuristics as if they were paper parameters,
- any remaining divergence is explicit, narrow, and documented.

## Non-Goals

- This plan does not attempt to complete WHIR.
- This plan does not attempt to prove the missing Galois-ring soundness arguments inside the repo; it only makes the implementation match the paper-facing protocol contract as closely as practical.
- This plan does not reopen unrelated performance work.

## Original Baseline

At the start of this repair line, the repo had:

- a sparse-opening PCS surface with `commit / open / verify`,
- correct virtual-quotient construction for the current `alpha in T \\ L` subset,
- prover/verifier self-consistency under the current prototype semantics,
- explicit benchmark disclaimers that the current soundness metadata is engineering-only.

The three gaps above were the basis for the phased repair below.

## Execution Strategy

Do not try to land this as one undifferentiated patch. Execute in four tracked phases:

1. Freeze the theorem-facing target and add guardrails.
2. Repair challenge sampling semantics.
3. Repair soundness parameterization and benchmark semantics.
4. Upgrade the public PCS surface from the current subset to a full `pi_FRICom`-style protocol path.

Each phase below includes code touchpoints, validation, and exit criteria.

## Phase 0: Freeze Target Semantics

### Objective

Before changing code, remove ambiguity about what "paper-aligned" means for this repo after the repair.

### Tasks

- [x] Re-read Procedure 5 and the surrounding Theorem 4.1 soundness text in:
  - `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md`
  - `mineru-md/Z2KSNARK(Full-Version)/hybrid_auto/Z2KSNARK(Full-Version).md`
- [x] Write a short design note inside this file's "Execution Notes" section answering:
  - whether the repaired implementation must support `alpha` sampled from all of `T` or may remain restricted to `T \\ L`,
  - whether the public PCS API should expose an explicit repetition count `m`,
  - whether the existing sparse PCS should be kept as a prototype API alongside the theorem-facing API or replaced in-place.
- [x] Mark the exact protocol contract that later phases will target:
  - theorem-facing PCS path,
  - benchmark/engineering path,
  - compatibility/debug-only path.

### Code Touchpoints

- `README.md`
- `include/fri/common.hpp`
- `include/fri/parameters.hpp`
- `bench/bench_time.cpp`

### Validation

- [x] No code changes yet beyond comments/docs.
- [x] The target semantics are written down in one place and are precise enough to review before implementation.

### Exit Criteria

- [x] There is no unresolved ambiguity about the target behavior of `beta_i`, `m`, and the external proof object.

## Phase 1: Repair Folding-Challenge Sampling

### Objective

Make theorem-facing FRI folding challenges match the paper's `beta_i <- T` sampling rule.

### Problem Statement

Current FRI uses `Transcript::challenge_ring(...)`, which deserializes arbitrary ring bytes. That produces a challenge in the ambient ring, not necessarily in `T`.

### Implementation Tasks

- [x] Add an explicit transcript/helper API for sampling from the exceptional set `T`.
- [x] Choose one concrete representation rule and document it:
  - sample an index in `[0, |T|-1]` with rejection sampling and map it deterministically to the corresponding Teichmuller representative, or
  - introduce a direct `challenge_teichmuller(...)` helper backed by the same rule.
- [x] Keep the current generic `challenge_ring(...)` API for non-paper paths; do not silently reuse it for theorem-facing FRI folding.
- [x] Change theorem-facing FRI folding to use the new `T`-sampling helper for every `beta_i`.
- [x] Re-check STIR separately before changing its fold challenge semantics; do not couple the STIR change to the FRI repair unless the paper target is confirmed.

### Suggested Code Touchpoints

- `include/crypto/fs/transcript.hpp`
- `src/crypto/fs/transcript.cpp`
- `include/algebra/teichmuller.hpp`
- `src/algebra/teichmuller.cpp`
- `src/fri/common.cpp`
- `src/fri/prover.cpp`
- `src/fri/verifier.cpp`
- `tests/test_crypto.cpp`
- `tests/test_fri.cpp`

### Required Tests

- [x] Add a unit test that theorem-facing FRI folding challenges are always in `T`.
- [x] Add a deterministic replay test showing prover and verifier derive identical `beta_i`.
- [x] Add a negative test that the old unrestricted challenge path is no longer used by theorem-facing FRI.
- [x] Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_crypto|test_fri'
```

### Exit Criteria

- [x] Every theorem-facing FRI `beta_i` is sampled from `T`.
- [x] The sampling rule is documented and test-covered.
- [x] No benchmark or verifier path still depends on generic ring sampling for paper-facing FRI folding.

## Phase 2: Repair Soundness Parameterization

### Objective

Expose theorem-facing FRI soundness through the paper's repetition parameter `m`
and stop routing public FRI behavior through repo-local engineering heuristics.

### Problem Statement

The paper's soundness statement uses a repetition parameter `m` that repeats Steps 3/4/5. The repo currently uses:

- manual per-round schedules `query_repetitions`,
- an auto heuristic driven by `rho`, `lambda_target`, and `pow_bits`,
- benchmark metadata such as `effective_security_bits` that is intentionally non-theorem.

These are valid engineering tools, but they are not the paper's soundness parameterization.
For this phase, `bench_time` only upgrades FRI to the paper-facing parameterization.
STIR keeps its current engineering schedule path and will be handled separately.

### Implementation Tasks

- [x] Introduce an explicit theorem-facing FRI parameter for repetition count, named so its purpose is obvious, e.g. `repetition_count` or `fri_repetitions`.
- [x] Define the exact mapping from that repetition count to query behavior:
  - each repetition is one independent query chain across all FRI rounds,
  - repeated chains share no transcript labels,
  - repeated chains are carried consistently through terminal checks.
- [x] Remove theorem-facing FRI dependence on the current engineering schedule path instead of keeping a mixed public FRI mode boundary.
- [x] Keep STIR's current engineering schedule path unchanged for now and document that it remains separate from theorem-facing FRI.
- [x] Stop using `lambda_target - pow_bits` to drive theorem-facing FRI query counts.
- [x] Update benchmark rows so they clearly report which soundness path was used.
- [x] Add an explicit field for theorem-facing repetition count when that mode is active.
- [x] Preserve `soundness_scope=engineering_metadata_non_paper` only for the STIR engineering path.
- [x] Decide whether `FriProver::prove(...)` remains an experimental low-level entry point while `commit/open/verify` becomes the main theorem-facing surface.

### Suggested Code Touchpoints

- `include/fri/parameters.hpp`
- `src/fri/parameters.cpp`
- `include/soundness/configurator.hpp`
- `src/soundness/configurator.cpp`
- `bench/bench_time.cpp`
- `README.md`
- `tests/test_fri.cpp`
- `tests/test_soundness_configurator.cpp`

### Required Tests

- [x] Add tests for theorem-facing repetition count to query-shape mapping.
- [x] Add tests showing theorem-facing FRI query-chain behavior is explicit and no longer tied to the old engineering FRI schedule assumptions.
- [x] Update or replace the current tests that hard-code the heuristic round shape as if it were the default FRI soundness contract.
- [x] Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_fri|test_soundness_configurator'
./build/bench_time --protocol fri3 --n 9 --d 8 --fri-repetitions 2 --format text
```

### Exit Criteria

- [x] Theorem-facing FRI uses an explicit repetition parameter instead of heuristic scheduling.
- [x] STIR engineering heuristics remain available, but are visibly marked as non-paper.
- [x] README and benchmark output can no longer blur FRI's paper parameter `m` with STIR's engineering metadata.

## Phase 3: Upgrade to Full `pi_FRICom` Semantics

### Objective

Move from the current sparse-opening PCS subset to a public PCS path that mirrors Procedure 5 closely enough to call it a full `pi_FRICom`-style implementation.

### Problem Statement

Current repo behavior differs from the full-version protocol shape in at least these ways:

- the public proof object is optimized around sparse openings plus `final_polynomial`,
- the verifier does not currently follow a one-to-one implementation of Procedure 5,
- the current code path was intentionally documented as a subset rather than the full construction.

### Implementation Tasks

- [x] Split protocol objects cleanly:
  - theorem-facing external proof/opening types,
  - optional prototype/compat witness types,
  - benchmark serialization over the actual external theorem-facing object.
- [x] Decide whether to keep the current sparse path under a separate experimental API.
- [x] Implement the exact round structure required by the target interpretation of Procedure 5.
- [x] Re-check the `alpha` semantics from Phase 0:
  - if full `alpha <- T` support is required, implement a path that handles `alpha in L` correctly instead of relying on pointwise division over `L`,
  - if the paper-facing subset still excludes `alpha in L`, document the proof/reference justification explicitly.
- [x] Replace the current "subset" README language once the theorem-facing path exists.
- [x] Rework serializer accounting so proof bytes for the theorem-facing PCS are computed from the real external object only.
- [x] Audit verifier logic line by line against Procedure 5 after implementation and record any remaining deltas in this file.

### Suggested Code Touchpoints

- `include/fri/common.hpp`
- `src/fri/common.cpp`
- `include/fri/prover.hpp`
- `src/fri/prover.cpp`
- `include/fri/verifier.hpp`
- `src/fri/verifier.cpp`
- `README.md`
- `bench/bench_time.cpp`
- `tests/test_fri.cpp`

### Required Tests

- [x] Honest end-to-end theorem-facing PCS roundtrip.
- [x] Tamper tests for each external proof component.
- [x] Serialization/proof-size tests for the new external object.
- [x] If `alpha in L` becomes supported, add explicit honest and reject-path tests for that case.
- [x] Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_fri'
```

### Exit Criteria

- [x] The repo has a theorem-facing public FRI PCS path that no longer needs the README caveat "paper-aligned subset".
- [x] External proof objects, verifier checks, and proof-byte accounting all refer to the same protocol shape.
- [x] Any residual divergence from the paper is documented in one short, explicit list.

## Phase 4: Bench, Docs, and Cleanup

### Objective

After the protocol repair lands, make the rest of the repo stop leaking the old semantics.

### Tasks

- [ ] Update `README.md` current-scope and paper-alignment sections.
- [ ] Update benchmark help text and emitted metadata fields.
- [ ] Remove or rename any stale fields whose names imply theorem-level soundness when they are heuristic-only.
- [ ] Add one narrow regression test for each bug fixed in Phases 1-3.
- [ ] Confirm no test still encodes the old heuristic schedule as the canonical paper-facing behavior.

### Validation

- [ ] Focused tests:

```bash
ctest --test-dir build --output-on-failure -R 'test_crypto|test_fri|test_soundness_configurator'
```

- [ ] Optional release verification after code lands:

```bash
ctest --test-dir build-release --output-on-failure -R 'test_crypto|test_fri|test_soundness_configurator'
OMP_NUM_THREADS=1 ./build-release/bench_time --protocol fri9 --threads 1 --format text
```

### Exit Criteria

- [ ] Docs, code, tests, and benchmark output all describe the same FRI soundness story.

## Recommended Patch Batching

Do not merge by file type. Merge by semantic milestone:

1. Patch set A: transcript + `T` sampling + tests.
2. Patch set B: theorem-facing repetition parameter + benchmark mode split + tests/docs.
3. Patch set C: full `pi_FRICom` proof object/prover/verifier migration + serialization + tests.
4. Patch set D: cleanup of README, benchmark text, and any deprecated compatibility path.

Each patch set should leave the repo in a buildable and testable state.

## Stop Rules

Stop and re-review before coding if any of these happens:

- the paper's `alpha` domain requirement remains ambiguous after Phase 0,
- the theorem-facing proof object cannot coexist cleanly with the current sparse prototype object,
- benchmark consumers depend on the current heuristic fields in a way that would make a silent rename dangerous.

## Tracking Checklist

- [x] Phase 0 completed
- [x] Phase 1 completed
- [x] Phase 2 completed
- [x] Phase 3 completed
- [ ] Phase 4 completed

## Execution Notes

Use this section during follow-up implementation to record:

- decisions on `alpha` support,
- final parameter names,
- PR/commit boundaries,
- any residual paper deltas that remain after the main repair.

### Phase 0 Semantics Freeze (2026-03-10)

- Paper anchor re-read completed against Procedure 5 / Theorem 4.1 in both extracted
  Markdown files. The theorem-facing target is no longer the current `alpha in T \\ L`
  subset. Later phases should target verifier challenges sampled from all of `T`,
  with `alpha in L` handled explicitly during the theorem-facing PCS upgrade instead
  of remaining an undocumented subset restriction.
- Theorem-facing public PCS APIs should expose an explicit repetition count `m`
  (or a clearly synonymous field name such as `fri_repetitions`). This is a
  paper-facing parameter and should not be inferred from the current engineering
  heuristic knobs `query_repetitions`, `lambda_target`, `pow_bits`, or `sec_mode`.
- This Phase 0 transition note was later superseded by the Phase 3 decision to
  replace the old sparse-opening public PCS path in place. It is kept here only as
  historical context for why later execution notes explicitly record that reversal.
- Target protocol contract for later phases:
  - theorem-facing PCS path: `alpha <- T`, `beta_i <- T`, explicit repetition count
    `m`, and an external proof/opening object that is audited against Procedure 5
    rather than against the current sparse-opening shortcut;
  - benchmark / engineering path: existing heuristic query scheduling and
    soundness-calibration metadata remain available, but are explicitly marked as
    non-paper and must not be presented as theorem-backed parameters;
  - compatibility / debug-only path: any temporary witness-heavy or sparse-shortcut
    helper surfaces remain non-theorem-facing and may be removed once the public
    theorem-facing migration lands.
- Residual deltas after Phase 0 remain intentional and tracked:
  - current code still rejects `alpha in L`;
  - current public benchmark CLI still keeps STIR engineering knobs, but FRI now
    has to move onto explicit theorem-facing repetition counts in Phase 2;
  - current public sparse-opening proof object remains a prototype path until the
    later theorem-facing upgrade lands. This note was resolved by Phase 3.

### Phase 1 Challenge Sampling Repair (2026-03-10)

- Landed in commit `bb2272c` (`fri: sample folding challenges from teichmuller set`).
- The transcript now exposes a dedicated theorem-facing helper
  `challenge_teichmuller(...)`. Its concrete rule is:
  - rejection-sample an integer index in `[0, |T|-1]`;
  - map that index deterministically to the maximal exceptional set
    `T = {0, 1, zeta, ..., zeta^(|T|-2)}` where `zeta` is the repo's
    deterministic Teichmuller generator.
- Theorem-facing FRI folding now routes through
  `derive_fri_folding_challenge(...)` instead of generic
  `Transcript::challenge_ring(...)`.
- The generic ring challenge API remains available for non-paper paths.
- Current STIR code was checked separately and intentionally left unchanged in
  this phase. It still uses the older generic challenge helper until STIR's own
  challenge semantics are reviewed.
- Validation completed for this phase:
  - `cmake --build build -j4`
  - `ctest --test-dir build --output-on-failure -R 'test_crypto|test_fri'`

### Phase 2 Soundness Parameterization Repair (2026-03-10)

- Theorem-facing FRI now exposes explicit repetition-count semantics through
  `FriParameters::repetition_count`, with `bench_time` and wrapper scripts
  surfacing the same idea as `--fri-repetitions`.
- The repo now implements the paper's "repeat Steps 3/4/5" interpretation of
  `m`:
  - round 0 starts `m` independent query chains,
  - later fold rounds carry those chains forward instead of fresh-sampling a new
    per-round budget,
  - terminal checks preserve chain multiplicity even when sparse openings dedupe
    physical query indices.
- The old theorem-facing FRI heuristic fields (`query_repetitions`,
  `lambda_target`, `pow_bits`, `sec_mode`) were removed from `FriParameters` and
  no longer affect FRI prover/verifier behavior.
- `bench_time` now reports:
  - FRI rows with `soundness_mode=theorem_fri`,
    `soundness_model=paper_repetition_count_m`, and explicit
    `fri_repetitions`;
  - STIR rows with `soundness_mode=engineering_stir` and the existing
    `engineering_metadata_non_paper` scope.
- Preset wrappers and the parameter-search entrypoint now pass an explicit
  `fri_repetitions` value through to `bench_time` so mixed-protocol benchmark
  runs still execute cleanly while STIR remains on its old engineering knobs.
- At Phase 2 landing, `FriProver::prove(...)` / `FriVerifier::verify(instance, proof)`
  still remained available as lower-level FRI proof paths under the same theorem-facing
  `m` semantics. Phase 3 later removed those low-level public entry points when the
  theorem-facing PCS migration replaced the old sparse-opening surface in place.
- Validation completed for this phase:
  - `cmake --build build -j4`
  - `ctest --test-dir build --output-on-failure -R 'test_fri|test_soundness_configurator'`
  - `./build/bench_time --protocol fri3 --n 9 --d 8 --fri-repetitions 2 --format text`
  - `./scripts/run_timing_benchmark_from_preset.sh --preset bench/presets/all_protocols_smoke_gr216_r54.json --build-dir build --warmup 0 --reps 1 --output /tmp/swgr_phase2_time.csv`

### Phase 3 Full `pi_FRICom`-Style PCS Upgrade (2026-03-10)

- The old public sparse-opening PCS surface was replaced in place rather than
  retained as a long-lived prototype API. The public `FriOpening` / `FriProof`
  object now follows the theorem-facing BCS-style PCS shape used by the repo:
  - `g_0` remains a virtual oracle and is not separately committed;
  - the prover commits to `g_i` roots only for `i >= 1`;
  - the final stage reveals the entire final oracle table instead of a
    `final_polynomial`.
- The public theorem-facing verifier now accepts `alpha <- T` across the full
  Teichmuller set, including `alpha in L`.
- The first-round `alpha in L` handling no longer relies on pointwise division
  over all of `L`; instead the verifier treats the `alpha` slot as the special
  virtual-oracle exception while checking the rest of the queried fiber against
  committed `f|L` values and the opened child oracle.
- The transcript / oracle order was upgraded toward Procedure 5:
  - absorb the commitment to `f`;
  - absorb `(alpha, v)`;
  - derive each `beta_i`, then commit and absorb each explicit `g_i` root;
  - only after the root chain is fixed derive the theorem-facing query chains.
- Proof-size accounting now comes from the actual theorem-facing external object:
  roots for `g_1, ..., g_{r'}`, per-round Merkle openings, and the revealed
  final oracle table.
- Residual paper deltas after Phase 3:
  - the repo keeps the Phase-2 carried-query-chain interpretation of repetition
    parameter `m` rather than re-sampling every `gamma_i` independently as the
    Procedure 5 prose suggests;
  - the zero-fold PCS opening path reveals the full committed oracle and full
    quotient oracle table together because there are no explicit folded-oracle
    rounds to mediate the virtual-oracle relation.
- Validation completed for this phase:
  - `cmake --build build -j4`
  - `ctest --test-dir build --output-on-failure -R 'test_fri'`
  - `./build/bench_time --protocol fri3 --n 9 --d 8 --fri-repetitions 2 --format text`
  - `./build/bench_time --protocol fri3 --n 9 --d 8 --stop-degree 1 --fri-repetitions 2 --format text`
