# CMeta-CFlow Calculus Lean Phase B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Formalize the v1 Source, Demand, WAIT/wake, cancellation, and Terminal small-step semantics in Lean and prove their safety invariants.

**Architecture:** `CFlow/Execution.lean` owns complete source continuations, separate source/output types, source advance, downstream emit, drain, WAIT registration, and terminal relations. Kernel-issued generation tokens make registrations fresh, while continuation-owned roots make suspension checks complete within the model. `Proofs/Execution.lean` proves demand, lost-wakeup, suspension, cancellation, drain, and terminal invariants without changing the C runtime; `Test/PhaseATests/PhaseB.lean` supplies positive witnesses and negative boundary checks under the existing test driver.

**Tech Stack:** Lean 4.33.1, Lake, inductive indexed relations, proposition-level ownership contexts

**Spec:** `docs/superpowers/specs/2026-08-22-cmeta-cflow-calculus-v1-design.md`

## Global Constraints

- Phase B covers only `Source / Demand / WAIT / Terminal` from section 22.
- Demand is a natural number, and one downstream value consumes exactly one unit.
- Terminal signals are not demand-gated.
- A terminal state admits no later value, error, done, WAIT, wake, arm, or cancellation step.
- WAIT is legal only when every live value is owned according to the authoritative ownership context.
- Signal-before-arm and signal-concurrent-with-arm must not leave the run suspended.
- Source WAIT returns a waitable; the Kernel issues a fresh wake token for each arm.
- Continuation live roots are part of the continuation itself, not an independent side table.
- Source values do not consume downstream demand; only Sink emission does.
- Source completion begins downstream drain and does not establish whole-run DONE early.
- The Lean model remains independent of C runtime changes; production C directories are out of scope.
- No new external dependency or build-system integration outside `formal/cmeta_cflow_calculus/` is introduced.
- Existing user-visible behavior and the staged `CMakeUserPresets.json` change remain untouched.

---

### Task 1: Source and runtime small-step vocabulary

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Execution.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseB.lean`

**Interfaces:**
- Consumes: `CMeta.Value`, `CMeta.OwnershipContext`, `CMeta.PackedValue`, and `CMeta.SuspendSafe` from Phase A.
- Produces: `SourceState`, `Demand.consume`, `Waitable`, `WaitToken`, `WaitState`, `SourceTerminal`, `DrainState`, `Terminal`, `RuntimeState`, `Config`, `SourceResult`, `SourceStep`, `EmitStep`, `DrainStep`, `FinishStep`, `ArmTiming`, `ArmStep`, `WakeStep`, `CancelStep`, `Event`, and `Step`.

- [x] **Step 1: Write the failing import and state-shape tests**

```lean
import CMetaCFlowCalculus.CFlow.Execution

example : Demand.finite 0 |>.consume = none := rfl
example : Demand.finite 2 |>.consume = some (.finite 1) := rfl
example : SourceResult scalarTy := .done
example : Terminal := .cancelled
```

- [x] **Step 2: Run the focused test and confirm the module is missing**

Run: `lake env lean Test/PhaseATests/PhaseB.lean`

Expected: FAIL because `CMetaCFlowCalculus.CFlow.Execution` does not exist.

- [x] **Step 3: Define the state vocabulary and relations**

```lean
inductive Demand where
  | finite (remaining : Nat)

def Demand.consume : Demand -> Option Demand
  | .finite 0 => none
  | .finite (n + 1) => some (.finite n)

inductive WaitState where
  | ready
  | pendingArm (waitable : Waitable)
  | suspended (waitable : Waitable) (token : WaitToken)

inductive Terminal where
  | running
  | done
  | error (message : String)
  | cancelled

structure RuntimeState where
  ownership : OwnershipContext
  demand : Demand
  wait : WaitState
  nextWakeGeneration : Nat
  sourceTerminal : SourceTerminal
  drain : DrainState
  terminal : Terminal
```

Add the five source results `value`, `valueAndDone`, `wait`, `done`, and `error`. Define `SourceStep` so source value polling requires positive demand without consuming it, WAIT requires positive demand plus `SuspendSafe` over `next.liveRoots`, and done/error require no demand. Define `EmitStep` as the only demand-consuming relation; source completion enters drain, and whole-run DONE requires an explicit drain and finish. Define fresh-token arm, wake, cancel, and their unified `Step` wrappers; every constructor requires `terminal = running`.

- [x] **Step 4: Run the focused test and confirm the vocabulary elaborates**

Run: `lake env lean Test/PhaseATests/PhaseB.lean`

Expected: PASS for the state-shape examples.

### Task 2: Demand and terminal invariants

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Execution.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseB.lean`

**Interfaces:**
- Consumes: `SourceStep`, `EmitStep`, `Step`, `Event.EmitsValue`, `Demand.consume`, and `Terminal` from Task 1.
- Produces: `step_value_decrements_demand`, `zero_demand_no_value`, and `terminal_no_step`.

- [x] **Step 1: Add failing theorem-use tests**

```lean
example (h : Step before event after) (emits : event.EmitsValue) :
    exists n, before.runtime.demand = .finite (n + 1) /\
      after.runtime.demand = .finite n :=
  step_value_decrements_demand h emits

example (terminal : before.runtime.terminal != .running) :
    Not (Step before event after) :=
  terminal_no_step terminal
```

- [x] **Step 2: Run the focused test and confirm the proof names are absent**

Run: `lake env lean Test/PhaseATests/PhaseB.lean`

Expected: FAIL with unknown identifiers for the Phase B proof theorems.

- [x] **Step 3: Prove exact demand consumption and terminal absorption**

Case-analyze the unified step. Only `EmitStep` can satisfy `Event.EmitsValue`; its constructor equation forces pre-demand `finite (n + 1)` and post-demand `finite n`. Prove source values preserve demand and prove `zero_demand_no_value` as a corollary. Invert every `Step` constructor to recover its `terminal = running` premise and contradict a non-running pre-state for `terminal_no_step`.

- [x] **Step 4: Run the focused test and confirm both invariants**

Run: `lake env lean Test/PhaseATests/PhaseB.lean`

Expected: PASS, including a zero-demand negative witness and a completed-state no-step witness.

### Task 3: WAIT, lost-wakeup, ownership, and cancellation invariants

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Execution.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseB.lean`

**Interfaces:**
- Consumes: `SourceStep.wait`, `ArmStep`, `WakeStep`, `CancelStep`, and Phase A `SuspendSafe`.
- Produces: `source_wait_requires_suspend_safe`, `wait_arm_wake_preserves_source`, `signal_before_arm_is_ready`, `signal_concurrent_with_arm_is_ready`, and `cancel_unarms_and_terminates`.

- [x] **Step 1: Add failing WAIT/wake and cancellation witnesses**

```lean
example (h : SourceStep before (.wait token nextSource) pending) :
    SuspendSafe before.runtime.ownership before.runtime.live :=
  source_wait_requires_suspend_safe h

example (h : CancelStep before after) :
    after.runtime.wait = .ready /\ after.runtime.terminal = .cancelled :=
  cancel_unarms_and_terminates h
```

Also add concrete owned-live WAIT, quiet arm, exact-token wake, signal-before-arm, signal-concurrent-with-arm, and cancellation derivations.

- [x] **Step 2: Run the focused test and confirm the WAIT proofs are absent**

Run: `lake env lean Test/PhaseATests/PhaseB.lean`

Expected: FAIL with unknown identifiers for the WAIT and cancellation theorem names.

- [x] **Step 3: Prove suspension and lost-wakeup safety by relation inversion**

Invert `SourceStep.wait` to expose its `SuspendSafe` premise. Compose a WAIT step, quiet arm step, and matching wake step to prove that the resumed configuration is ready at the same source continuation. Invert the two signaled arm constructors to prove they return directly to ready. Invert cancellation to prove it clears pending/suspended registration and establishes the single terminal fact `cancelled`.

- [x] **Step 4: Run the focused test and confirm all WAIT paths**

Run: `lake env lean Test/PhaseATests/PhaseB.lean`

Expected: PASS for quiet wake, both lost-wakeup races, owned-live suspension, and cancel-unarm behavior.

### Task 4: Library integration and complete verification

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: the Phase B execution module, proof module, and tests from Tasks 1-3.
- Produces: a single root import surface and a single `lake test` entry point covering Phases A and B.

- [x] **Step 1: Add root imports and include Phase B tests in the driver**

```lean
import CMetaCFlowCalculus.CFlow.Execution
import CMetaCFlowCalculus.Proofs.Execution
```

Add `import PhaseATests.PhaseB` to `Test/PhaseATests.lean` so the existing test driver elaborates both phases.

- [x] **Step 2: Run focused and library verification**

Run: `lake env lean Test/PhaseATests/PhaseB.lean`

Expected: PASS.

Run: `lake test -v`

Expected: PASS with both Phase A and Phase B tests.

Run: `lake build -v`

Expected: PASS for the default library target.

- [x] **Step 3: Scan for proof escapes and scope drift**

Run: `rg.exe -n "\b(sorry|admit|axiom)\b" CMetaCFlowCalculus Test`

Expected: no matches.

Run: `git diff --name-only -- cflow cmeta container cmake CMakeLists.txt`

Expected: no Phase B production-code changes; the pre-existing staged preset change remains outside the formal patch.

- [x] **Step 4: Review the final diff against sections 8 and 22 of the spec**

Confirm all five source outcomes exist, only downstream emit consumes demand, source completion drains before whole-run DONE, error/cancel are immediate terminal states, terminal is absorbing, WAIT checks continuation-owned roots, every arm issues a fresh generation, both arm races become ready, stale-token wake is rejected, exact-token wake preserves the continuation payload, and cancellation clears wait state.

### Task 5: Independent review remediation

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Execution.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Execution.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseB.lean`

**Interfaces:**
- Consumes: the Phase B relations and first independent review findings.
- Produces: complete continuation roots, fresh wait generations, source/emit separation, drain-before-terminal, and strengthened suspension preservation.

- [x] **Step 1: Bind suspension safety to the saved continuation**

Replace the numeric continuation with `SourceState.cursor` plus `SourceState.liveRoots`, remove the independent runtime live list, and require `SuspendSafe ownership next.liveRoots` in `SourceStep.wait`.

- [x] **Step 2: Separate waitables from fresh Kernel tokens**

Make Source return `Waitable`; make arm issue `WaitToken { generation := nextWakeGeneration }` and increment the generation. Verify a stale token from a prior registration cannot wake a new suspension using the same waitable.

- [x] **Step 3: Separate source production from downstream emission**

Require positive demand to poll `SourceStep.value/valueAndDone` while preserving that demand, introduce demand-consuming `EmitStep`, and route source completion through `DrainStep` then `FinishStep`. Verify zero-demand source polling is rejected, filter-drop-compatible demand preservation holds, and pending-final-value drain remains possible.

- [x] **Step 4: Strengthen continuation and terminal regression tests**

Prove wake preserves source, ownership, demand, source terminal, drain, and run terminal while advancing generation once. Cover pending/suspended cancellation, source error absorption, zero-demand WAIT rejection, mismatched arm/wake, and zero-demand drain/finish.
