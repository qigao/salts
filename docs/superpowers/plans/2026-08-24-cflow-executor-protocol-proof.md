# CFlow Executor Protocol Proof Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an executable Lean model proving bounded Executor admission, task conservation, serial safety, shutdown settlement, callback self-block rejection, and Machine scheduling composition.

**Architecture:** Add a standalone `ExecutorProtocol` state whose task-phase ledger is the accounting fact source and whose counters are derived, then pair it with the existing `WorkerPhase` for the two scheduling handoff transitions. Keep safety proofs independent of OS fairness and leave C runtime code unchanged.

**Tech Stack:** Lean 4, Lake, existing CMeta/CFlow calculus and Phase A executable tests.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-executor-protocol-proof-design.md`

## Global Constraints

- Change no C API, C implementation, generated header, Graph IR, or serialized format.
- Capacity bounds queued work only; running work is accounted separately.
- Accepted tasks have exactly one accounting category at every modeled state.
- Shutdown rejects new admission and distinguishes drain from cancel-pending.
- Blocking operations that would self-wait in the same Executor callback context fail fast.
- Machine scheduled/executing phases require accepted/started Executor work.
- Claim no unconditional OS scheduling or task-termination liveness.
- Introduce no proof escape through `sorry`, `axiom`, `admit`, or `unsafe`.

---

### Task 1: Establish the failing Executor proof surface

**Files:**
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/ExecutorProtocol.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: intended modules `CFlow.ExecutorProtocol` and `Proofs.ExecutorProtocol`.
- Produces: examples for capacity, conservation, serial admission, shutdown, self-block rejection, and Machine composition.

- [x] **Step 1: Add imports and executable protocol examples**

- [x] **Step 2: Run `lake test` and record RED**

Expected failure: Lean cannot resolve `CMetaCFlowCalculus.CFlow.ExecutorProtocol`.

### Task 2: Implement the Executor transition model

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/ExecutorProtocol.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`

**Interfaces:**
- Produces: executor kind/lifecycle/policy/context/result enums, accounting state, safety predicates, admission/start/finish/shutdown/close transitions, and Machine paired transitions.

- [x] **Step 1: Define state, derived safety predicates, and initial state**

- [x] **Step 2: Define fail-fast admission and execution transitions**

- [x] **Step 3: Define drain/cancel-pending shutdown settlement and close**

- [x] **Step 4: Define Machine idle/scheduled/executing composition**

- [x] **Step 5: Build the model module**

Run `lake build CMetaCFlowCalculus.CFlow.ExecutorProtocol`.

### Task 3: Prove Executor and Machine safety

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/ExecutorProtocol.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`

**Interfaces:**
- Produces theorem families for initial safety, transition preservation, exclusive settlement, shutdown rejection, quiescent close, self-block fail-fast, and Machine scheduling refinement.

- [x] **Step 1: Prove initial, admission, start, and finish invariants**

- [x] **Step 2: Prove both shutdown policy outcomes and post-shutdown rejection**

- [x] **Step 3: Prove callback self-block rejection and accounting preservation**

- [x] **Step 4: Prove Machine scheduling and execution require accepted/started tasks**

- [x] **Step 5: Run focused GREEN builds**

Run `lake build CMetaCFlowCalculus.Proofs.ExecutorProtocol` and
`lake build PhaseATests.ExecutorProtocol`.

### Task 4: Verify and commit the complete calculus

**Files:**
- Modify: this plan only to record completed steps and exact evidence.

**Interfaces:**
- Consumes: all Executor model, proof, and executable test modules.
- Produces: merge-ready formal evidence without C runtime claims.

- [x] **Step 1: Run serial `lake build` and `lake test`**

- [x] **Step 2: Scan changed Lean files for proof escapes**

- [x] **Step 3: Run `git diff --check` and CodeGraph affected analysis**

- [x] **Step 4: Commit with `proof(cflow): model executor admission and shutdown`**

## Verification Evidence

- RED: before the model existed, `lake test` failed to resolve
  `CMetaCFlowCalculus.CFlow.ExecutorProtocol` and
  `CMetaCFlowCalculus.Proofs.ExecutorProtocol`.
- Focused GREEN: `lake build CMetaCFlowCalculus.Proofs.ExecutorProtocol`
  completed 12 jobs; `lake build PhaseATests.ExecutorProtocol` completed 13
  jobs.
- Full GREEN: serial `lake build` completed 50 jobs and `lake test` completed
  all 69 Phase A jobs.
- Proof-escape scan: no `sorry`, `axiom`, `admit`, or `unsafe` in the new model,
  proof, or executable test modules.
- `git diff --check` completed with no output.
- CodeGraph synchronized successfully. Its current analyzer reported no
  affected test files for the Lean additions, so Lake is the authoritative
  impact check.
