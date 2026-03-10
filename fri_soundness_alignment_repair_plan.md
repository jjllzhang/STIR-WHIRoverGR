# FRI Soundness Alignment Repair Plan

Date: 2026-03-10

Owner: Codex + user follow-up execution

Status: Drafted for execution tracking

## Goal

Repair the three currently known gaps between the repository's FRI implementation and the Z2KSNARK paper/full-version soundness story:

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

## Verified Current Baseline

The current repo already has:

- a sparse-opening PCS surface with `commit / open / verify`,
- correct virtual-quotient construction for the current `alpha in T \\ L` subset,
- prover/verifier self-consistency under the current prototype semantics,
- explicit benchmark disclaimers that the current soundness metadata is engineering-only.

The three gaps above remain real and should be fixed in that order.

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

- [ ] Add an explicit transcript/helper API for sampling from the exceptional set `T`.
- [ ] Choose one concrete representation rule and document it:
  - sample an index in `[0, |T|-1]` with rejection sampling and map it deterministically to the corresponding Teichmuller representative, or
  - introduce a direct `challenge_teichmuller(...)` helper backed by the same rule.
- [ ] Keep the current generic `challenge_ring(...)` API for non-paper paths; do not silently reuse it for theorem-facing FRI folding.
- [ ] Change theorem-facing FRI folding to use the new `T`-sampling helper for every `beta_i`.
- [ ] Re-check STIR separately before changing its fold challenge semantics; do not couple the STIR change to the FRI repair unless the paper target is confirmed.

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

- [ ] Add a unit test that theorem-facing FRI folding challenges are always in `T`.
- [ ] Add a deterministic replay test showing prover and verifier derive identical `beta_i`.
- [ ] Add a negative test that the old unrestricted challenge path is no longer used by theorem-facing FRI.
- [ ] Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_crypto|test_fri'
```

### Exit Criteria

- [ ] Every theorem-facing FRI `beta_i` is sampled from `T`.
- [ ] The sampling rule is documented and test-covered.
- [ ] No benchmark or verifier path still depends on generic ring sampling for paper-facing FRI folding.

## Phase 2: Repair Soundness Parameterization

### Objective

Separate theorem-facing FRI soundness parameters from repo-local engineering heuristics.

### Problem Statement

The paper's soundness statement uses a repetition parameter `m` that repeats Steps 3/4/5. The repo currently uses:

- manual per-round schedules `query_repetitions`,
- an auto heuristic driven by `rho`, `lambda_target`, and `pow_bits`,
- benchmark metadata such as `effective_security_bits` that is intentionally non-theorem.

These are valid engineering tools, but they are not the paper's soundness parameterization.

### Implementation Tasks

- [ ] Introduce an explicit theorem-facing FRI parameter for repetition count, named so its purpose is obvious, e.g. `repetition_count` or `fri_repetitions`.
- [ ] Define the exact mapping from that repetition count to query behavior:
  - each repetition is one independent query chain across all FRI rounds,
  - repeated chains share no transcript labels,
  - repeated chains are carried consistently through terminal checks.
- [ ] Keep the current engineering schedule path, but move it behind an explicit mode boundary such as:
  - theorem-facing mode, and
  - engineering/benchmark mode.
- [ ] Stop using `lambda_target - pow_bits` to drive theorem-facing FRI query counts.
- [ ] Update benchmark rows so they clearly report which mode was used.
- [ ] Add an explicit field for theorem-facing repetition count when that mode is active.
- [ ] Preserve `soundness_scope=engineering_metadata_non_paper` only for the engineering path.
- [ ] Decide whether `FriProver::prove(...)` remains an experimental low-level entry point while `commit/open/verify` becomes the main theorem-facing surface.

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

- [ ] Add tests for theorem-facing repetition count to query-shape mapping.
- [ ] Add tests showing engineering mode and theorem-facing mode produce different metadata and are never conflated.
- [ ] Update or replace the current tests that hard-code the heuristic round shape as if it were the default FRI soundness contract.
- [ ] Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_fri|test_soundness_configurator'
./build/bench_time --protocol fri3 --n 9 --d 8 --queries auto --format text
```

### Exit Criteria

- [ ] Theorem-facing FRI uses an explicit repetition parameter instead of heuristic scheduling.
- [ ] Engineering heuristics remain available, but are visibly marked as non-paper.
- [ ] README and benchmark output can no longer be misread as theorem-level security claims.

## Phase 3: Upgrade to Full `pi_FRICom` Semantics

### Objective

Move from the current sparse-opening PCS subset to a public PCS path that mirrors Procedure 5 closely enough to call it a full `pi_FRICom`-style implementation.

### Problem Statement

Current repo behavior differs from the full-version protocol shape in at least these ways:

- the public proof object is optimized around sparse openings plus `final_polynomial`,
- the verifier does not currently follow a one-to-one implementation of Procedure 5,
- the current code path was intentionally documented as a subset rather than the full construction.

### Implementation Tasks

- [ ] Split protocol objects cleanly:
  - theorem-facing external proof/opening types,
  - optional prototype/compat witness types,
  - benchmark serialization over the actual external theorem-facing object.
- [ ] Decide whether to keep the current sparse path under a separate experimental API.
- [ ] Implement the exact round structure required by the target interpretation of Procedure 5.
- [ ] Re-check the `alpha` semantics from Phase 0:
  - if full `alpha <- T` support is required, implement a path that handles `alpha in L` correctly instead of relying on pointwise division over `L`,
  - if the paper-facing subset still excludes `alpha in L`, document the proof/reference justification explicitly.
- [ ] Replace the current "subset" README language once the theorem-facing path exists.
- [ ] Rework serializer accounting so proof bytes for the theorem-facing PCS are computed from the real external object only.
- [ ] Audit verifier logic line by line against Procedure 5 after implementation and record any remaining deltas in this file.

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

- [ ] Honest end-to-end theorem-facing PCS roundtrip.
- [ ] Tamper tests for each external proof component.
- [ ] Serialization/proof-size tests for the new external object.
- [ ] If `alpha in L` becomes supported, add explicit honest and reject-path tests for that case.
- [ ] Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_fri'
```

### Exit Criteria

- [ ] The repo has a theorem-facing public FRI PCS path that no longer needs the README caveat "paper-aligned subset".
- [ ] External proof objects, verifier checks, and proof-byte accounting all refer to the same protocol shape.
- [ ] Any residual divergence from the paper is documented in one short, explicit list.

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
- [ ] Phase 1 completed
- [ ] Phase 2 completed
- [ ] Phase 3 completed
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
- The current sparse-opening PCS should remain available as a prototype /
  benchmark-oriented API during the transition. Do not replace it in place during
  the soundness repair. Later phases should introduce or route through a distinct
  theorem-facing PCS path rather than silently changing the semantics of the current
  prototype surface.
- Target protocol contract for later phases:
  - theorem-facing PCS path: `alpha <- T`, `beta_i <- T`, explicit repetition count
    `m`, and an external proof/opening object that is audited against Procedure 5
    rather than against the current sparse-opening shortcut;
  - benchmark / engineering path: existing heuristic query scheduling and
    soundness-calibration metadata remain available, but are explicitly marked as
    non-paper and must not be presented as theorem-backed parameters;
  - compatibility / debug-only path: current witness-heavy or sparse-shortcut
    helper surfaces may remain temporarily for transition and debugging, but must
    stay labeled as non-theorem-facing.
- Residual deltas after Phase 0 remain intentional and tracked:
  - current code still rejects `alpha in L`;
  - current public benchmark CLI still speaks in engineering scheduling knobs;
  - current public sparse-opening proof object remains a prototype path until the
    later theorem-facing upgrade lands.
