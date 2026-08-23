# CFlow Certified Rewrite Phase F-4A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove duplicate idempotent Map elimination and finite optimizer-certificate composition without treating a CMeta property declaration as its semantic law.

**Architecture:** Keep R1–R15 stable and place optimizer implementation certificates in a separate indexed Lean relation. The callable producer supplies an explicit idempotence law; Lean proves both one-step deletion and transitive preservation of the complete observable stream result.

**Tech Stack:** Lean 4.33.1, Lake, CMeta-CFlow Calculus v1.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-certified-rewrite-phase-f4a-design.md`

## Global Constraints

- Do not renumber or reinterpret R1–R15.
- A declared `IDEMPOTENT` property never implies `IdempotentLaw` inside Lean.
- Preservation covers effects, ordered values, terminal result and ownership safety.
- This phase changes no C ABI or runtime behavior.
- Do not stage or alter unrelated `CMakeUserPresets.json` or TinyTest changes.

---

### Task 1: Lock the missing proof boundary with Phase F

**Files:**
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseF.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: `UnaryMeaning`, `StreamResult.map`, Phase D scalar fixtures.
- Produces: compile-time requirements for `IdempotentLaw`, `IdempotentEndomapPremises`, `CertifiedRewrite`, `map_idempotent_elimination` and `certified_rewrite_preserves_observations`.

- [x] **Step 1: Add a clamp endomap example with a hand-proved idempotence law**

Use `min value.token 10`, declare PURE/TOTAL/IDEMPOTENT and require two Maps to equal one.

- [x] **Step 2: Add a three-Map composed-certificate example**

Construct two `CertifiedRewrite.idempotentMap` steps and combine them with
`CertifiedRewrite.trans`; require the three-Map result to equal one Map.

- [x] **Step 3: Add the dishonest declaration counterexample**

Declare `x + 1` as IDEMPOTENT, then prove `¬IdempotentLaw dishonestMeaning` by
specializing the alleged law at token zero.

- [x] **Step 4: Run `lake test` and verify RED**

Expected failure: all five required proof identifiers are unknown. This proves
the test covers the absent formal boundary rather than existing behavior.

### Task 2: Implement the law and certificate relation

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Rewrite.lean`

**Interfaces:**
- Produces: `IdempotentLaw`, `IdempotentEndomapPremises` and the indexed
  `CertifiedRewrite` constructors `refl`, `idempotentMap`, and `trans`.

- [x] **Step 1: Separate semantic law from callable properties**

Define `IdempotentLaw meaning := ∀ value, meaning.apply (meaning.apply value) = meaning.apply value` and package it beside PURE/TOTAL/IDEMPOTENT premises.

- [x] **Step 2: Add the indexed certificate relation**

Index the relation by the stream element type so the idempotent constructor can
specialize to `Value ty` while `refl` and `trans` remain generic.

- [x] **Step 3: Build and confirm the relation type-checks**

Run `lake test`; proceed only after the production module builds and any
remaining failure is in the proof implementation.

### Task 3: Prove one-step and composed preservation

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Rewrite.lean`

**Interfaces:**
- Consumes: `IdempotentEndomapPremises` and `CertifiedRewrite`.
- Produces: `map_idempotent_elimination` and
  `certified_rewrite_preserves_observations`.

- [x] **Step 1: Prove list-level duplicate Map elimination**

Induct on the input list and rewrite every head with the supplied idempotence law.

- [x] **Step 2: Lift the list theorem to complete StreamResult equality**

Destructure the stream; leave construction effects, outcome and ownership flag
unchanged while rewriting only the values field.

- [x] **Step 3: Prove certificate composition by induction**

Handle `refl` with reflexivity, `idempotentMap` with the single-rule theorem and
`trans` with `Eq.trans`.

- [x] **Step 4: Run all Lean tests to verify GREEN**

Run `lake test` from `formal/cmeta_cflow_calculus`; require Phase A–F to build
without warnings.

### Task 4: Review and deliver F-4A

**Files:**
- Create: `docs/superpowers/specs/2026-08-23-cflow-certified-rewrite-phase-f4a-design.md`
- Create: `docs/superpowers/plans/2026-08-23-cflow-certified-rewrite-phase-f4a.md`

**Interfaces:**
- Produces: a documented trust boundary and a reviewable formal-only commit.

- [x] **Step 1: Document the Lean/C trust boundary and deferred C checker**

State explicitly that Lean proves a conditional rule schema, CMeta supplies a
trusted declaration, and F-4B must verify trace/Graph correspondence.

- [x] **Step 2: Run final verification**

Run `lake test`, `git diff --check`, inspect only the formal/docs diff and confirm
unrelated working-tree changes remain untouched.

- [ ] **Step 3: Commit and push only F-4A files**

Stage the two Lean implementation files, Phase F test/import and these two docs;
commit as `formal: prove certified idempotent rewrite`, then push
`formal/cmeta-cflow-calculus-v1-v2`.
