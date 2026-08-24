# CFlow Managed Machine State Proof Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an executable Lean resource model proving lifecycle-safe staged-state cancellation, commit, and terminal disposal for non-trivial Machine state values.

**Architecture:** Add a separate `ManagedMachineState` refinement over the existing `ControlLifecycle` and `WorkerPhase`. A token-indexed ledger is the resource fact source; source/staged slots and construction/destruction counters expose balance and exactly-once obligations without changing the existing Machine model.

**Tech Stack:** Lean 4, Lake, existing CMeta/CFlow calculus and Phase A executable tests.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-managed-machine-state-proof-design.md`

## Global Constraints

- Preserve the existing `CFlow -> CMeta` dependency direction.
- Change no C API, C implementation, generated header, Graph IR, or serialized format.
- Reuse `ControlLifecycle`, `WorkerPhase`, `Ty`, and semantic type equality.
- Use a single token ledger as the resource status fact source.
- Keep copy failure transactional and make cancel/commit/dispose resource outcomes explicit.
- Introduce no proof escape through `sorry`, `axiom`, `admit`, or `unsafe`.

---

### Task 1: Establish the failing managed-state proof surface

**Files:**
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/ManagedMachineState.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: the intended module names `CFlow.ManagedMachineState` and `Proofs.ManagedMachineState`.
- Produces: executable examples for copy failure, cancel-wins, commit-wins, balance, disposal, and second-dispose rejection.

- [x] **Step 1: Add the test module imports and concrete managed values**

Use distinct tokens for a source and target value and state the expected theorem-driven examples.

- [x] **Step 2: Run `lake test` and record RED**

Expected failure: Lean cannot resolve `CMetaCFlowCalculus.CFlow.ManagedMachineState` because the production model does not exist.

### Task 2: Implement the managed resource transition model

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/ManagedMachineState.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`

**Interfaces:**
- Produces: `ManagedValue`, `ResourcePhase`, `ResourceLedger`, `ManagedControl`, `liveCount`, `Balanced`, `ready`, `stageCopy`, `requestCancel`, `beginCommit`, `commit`, `discardCancelled`, and `dispose`.

- [x] **Step 1: Define token-indexed resource status and ledger update**

The record stores semantic type plus `live` or `destroyed`; update replaces exactly one token.

- [x] **Step 2: Define managed control and balance**

The derived live count is the number of occupied source/staged slots. Balance is `constructed = destroyed + liveCount`.

- [x] **Step 3: Define pure lifecycle operations**

Copy failure returns the original state; successful staging creates a fresh target; cancel and commit consume staged state through the existing arbitration phases; disposal accepts only terminal idle state with one committed source.

- [x] **Step 4: Build the model module**

Run `lake build CMetaCFlowCalculus.CFlow.ManagedMachineState` and resolve only model/type errors.

### Task 3: Prove resource safety for both arbitration outcomes

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/ManagedMachineState.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`

**Interfaces:**
- Produces theorem families for failed-copy preservation, cancel-wins resource settlement, commit-wins resource settlement, balance after settlement/disposal, and second-dispose rejection.

- [x] **Step 1: Prove failed-copy preservation and initial balance**

The proof must establish equality of the full control state, not merely the committed token.

- [x] **Step 2: Prove cancel-before-commit settlement**

For distinct source/target tokens, prove source remains live, target is destroyed, completed stays zero, cancelled becomes one, and balance holds.

- [x] **Step 3: Prove begin-before-cancel commit settlement**

Prove old source is destroyed, target becomes the sole live committed source, completed becomes one, cancelled stays zero, and balance holds.

- [x] **Step 4: Prove terminal disposal and exactly-once rejection**

After either settlement, prove disposal produces zero live slots and complete balance, then prove a second disposal returns `none`.

- [x] **Step 5: Run focused GREEN builds**

Run `lake build CMetaCFlowCalculus.Proofs.ManagedMachineState` and `lake build PhaseATests.ManagedMachineState`.

### Task 4: Verify the complete calculus

**Files:**
- Modify: this plan only to record completed steps and exact evidence.

**Interfaces:**
- Consumes: all managed-state model and proof modules.
- Produces: merge-ready formal evidence without C runtime claims.

- [x] **Step 1: Run `lake build` and `lake test`**

- [x] **Step 2: Search the changed Lean files for proof escapes**

Run `rg.exe -n '\b(sorry|axiom|admit|unsafe)\b'` over the model, proof, and test modules; no matches are accepted.

- [x] **Step 3: Run `git diff --check` and CodeGraph affected analysis**

- [x] **Step 4: Commit the formal model and verification evidence**

Use commit message `proof(cflow): model managed machine state lifecycle`.

## Verification Evidence

- RED: before the model existed, `lake test` failed to resolve
  `CMetaCFlowCalculus.CFlow.ManagedMachineState`; after adding freshness
  admission, the duplicate-token example failed until `stageCopy` rejected an
  occupied ledger token.
- Focused GREEN: `lake build CMetaCFlowCalculus.Proofs.ManagedMachineState`
  completed 12 jobs and `lake build PhaseATests.ManagedMachineState` completed
  13 jobs.
- Full GREEN: `lake build` completed 48 jobs and `lake test` completed all 66
  Phase A targets.
- Proof-escape scan: no `sorry`, `axiom`, `admit`, or `unsafe` in the new model,
  proof, or test modules.
- CodeGraph: index synchronized; its current analyzer reported no affected test
  files for the Lean changes, so Lake remains the authoritative impact check.
