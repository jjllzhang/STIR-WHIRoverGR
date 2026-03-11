# Conservative Theorem-Facing STIR over GR Soundness Note

## Purpose

This note pins the first theorem-facing `STIR(9->3)` landing in this repository.
It is intentionally narrower than the finite-field STIR paper theorem. The goal is
to document the exact conservative claim that the live repo implements today.

## First-Landing Theorem Statement

The current theorem-facing STIR landing should be read as follows:

- folding and combine challenges are sampled from the Teichmuller set `T`
- theorem OOD and shake points are sampled from an explicit exceptional safe complement inside `T*`
- theorem OOD is restricted to a unique-decoding regime
- theorem validation rejects parameter sets that fall outside the supported conservative GR regime
- theorem benchmark metadata is derived from a conservative GR proximity envelope and does not claim Johnson/list-decoding alignment

This is a conservative theorem-facing landing. It does not claim that the
finite-field STIR Appendix C theorem has already been migrated verbatim to
Galois rings.

## External Dependencies

The current theorem-facing STIR soundness path depends on the following GR facts
from the Z2KSNARK papers.

From `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md`:

- the maximal Teichmuller set `T` is an exceptional set
- non-zero polynomials over `GR(p^k, r)` obey a Schwartz-Zippel style root bound over `T`
- Reed-Solomon codes over exceptional subsets of `T` retain unique interpolation and minimum-distance structure needed for unique-decoding reasoning

From `mineru-md/Z2KSNARK(Full-Version)/hybrid_auto/Z2KSNARK(Full-Version).md`:

- Theorem 3.3 gives a `(1-rho)/2` GR proximity gap over exceptional sets
- the Polishchuk-Spielman style argument over Galois rings yields a conservative probability envelope of the form `min(1, m * ell^2 / |T|)` for random Teichmuller linear combinations
- multiplicative subgroup domains inside `T*` remain compatible with the exceptional-set reasoning used by the current theorem-mode samplers and validators

## Field-STIR Lemmas Replaced By GR Variants

The current landing does not implement field-paper-equivalent versions of the
following STIR lemmas. Instead, it uses conservative GR replacements.

- field `Lemma 4.9` is replaced by a conservative GR folding-soundness envelope derived from the existing Z2KSNARK proximity-gap result
- field `Lemma 4.13` is replaced by a conservative GR degree-correction / combine envelope using the same Z2KSNARK proximity-gap machinery plus a random-point root bound
- field `Lemma 4.4` is replaced operationally by theorem-mode unique-OOD sampling and a GR quotient-by-wrong-values check backed by exceptional-set root bounds

These replacements are sufficient for the current benchmark metadata path, but
they are not yet packaged as a paper-complete GR-STIR theorem.

## Implemented Conservative Formula

For the current theorem-facing landing:

- `epsilon_out = 0`
- `epsilon_fold` uses `min(1, m * ell^2 / |T|)`
- `epsilon_shift` is decomposed as:
  - `(1 - delta)^t`
  - a conservative GR degree-correction term from a random-point root bound over `T`
  - a conservative GR folding term from the same `min(1, m * ell^2 / |T|)` envelope
- `epsilon_fin = (1 - delta)^t`

The current implementation reports `feasible=false` when the conservative GR
regime becomes trivial, for example when `delta <= 0` or when `ell^2` is too
large relative to `|T|` for the `(1-rho)/2` envelope to remain non-trivial.

## Assumption-Only Items That Remain Open

The following parts remain assumption-backed in the first landing.

- there is not yet a paper-complete GR-STIR theorem matching the full finite-field STIR Johnson/list-decoding path
- the current degree-correction term is a conservative GR envelope, not a field-paper-equivalent closed form
- the current theorem-facing mode does not claim a stronger guarded half-gap mode beyond the existing conservative Z2KSNARK-backed regime

## Repository Contract

When the repository prints:

- `soundness_mode=theorem_gr_conservative`
- `soundness_model=epsilon_rbr_stir_gr_conservative_unique_ood`
- `soundness_scope=theorem_gr_conservative_existing_z2ksnark_results`

it means exactly this note, and no more. In particular, it does not mean that
the repository is claiming a field-paper-equivalent STIR-over-GR theorem.
