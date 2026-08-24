# IO Actor and Executor Lean Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add executable Lean models and safety proofs for the bounded command
mailbox, generic Executor, and single-owner IO Actor protocol that Reactor and
Proactor backends will refine.

**Architecture:** Keep the three transition systems independent and compose
them only at dispatch. Store completion in the bounded request slot so Executor
backpressure cannot lose an accepted operation. Treat Disruptor, epoll, kqueue,
and IOCP as later concrete refinements rather than semantic primitives.

**Tech Stack:** Lean 4.33.1, Lake, existing `CMetaCFlowCalculus` pure transition
and theorem style.

**Spec:** `docs/superpowers/specs/2026-08-24-io-executor-lean-protocol.md`

## Global Constraints

- Preserve all existing CFlow and formal imports and behavior.
- Add no C/C++ API or dependency in this phase.
- Every queue and request collection has a positive hard capacity.
- Failed admission is transactional and leaves the state unchanged.
- Successful request admission owns its lease until acknowledgement.
- Safety proofs are unconditional; liveness remains deferred behind explicit
  fairness assumptions.

---

### Task 1: Define and prove the bounded MPSC mailbox

**Files:**

- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/IOBoundedMpsc.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/BoundedMpsc.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/IOBoundedMpsc.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**

- Consumes: Lean `List` and `Nat` only.
- Produces: `IO.BoundedMpsc.State`, `Admission`, `Observation`, `tryPublish`,
  `tryConsume`, and `close`.

- [ ] **Step 1: Write the failing behavior and proof tests**

  Add examples importing the absent proof module and requiring:

  ```lean
  example : (tryPublish empty 11).1 = .accepted := by native_decide
  example : (tryPublish full 12).1 = .full := by native_decide
  example : (tryConsume one).1 = .item 11 := by native_decide
  example : (tryPublish (close empty).2 11).1 = .closed := by native_decide
  example : (tryPublish empty 11).2.Valid :=
    tryPublish_preserves_valid empty 11 emptyValid
  ```

- [ ] **Step 2: Run the focused test and verify RED**

  Run from `formal/cmeta_cflow_calculus`:

  ```powershell
  lake env lean Test/PhaseATests/IOBoundedMpsc.lean
  ```

  Expected: failure because `CMetaCFlowCalculus.Proofs.IOBoundedMpsc` does not
  exist.

- [ ] **Step 3: Add the minimum executable mailbox model**

  Define a generic state with positive capacity, FIFO list, and
  `open/draining` terminal state. `tryPublish` returns
  `accepted/full/closed`; `tryConsume` returns `item/empty/closed`.

- [ ] **Step 4: Add the minimum safety proofs**

  Prove `tryPublish_preserves_valid`, `tryConsume_preserves_valid`,
  `accepted_appends_once`, `rejected_publish_unchanged`,
  `consume_observes_fifo_head`, and `close_preserves_queue`.

- [ ] **Step 5: Run focused and aggregate tests**

  ```powershell
  lake env lean Test/PhaseATests/IOBoundedMpsc.lean
  lake test
  ```

- [ ] **Step 6: Commit the independently testable mailbox model**

  ```powershell
  git add formal/cmeta_cflow_calculus
  git commit -m "formal(io): prove bounded command mailbox"
  ```

### Task 2: Define and prove Executor admission and execution

**Files:**

- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/IOExecutor.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/Executor.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/IOExecutor.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**

- Consumes: arbitrary task payloads.
- Produces: `IO.Executor.State`, `Capability`, `Terminal`, `tryPost`, `start`,
  `finish`, `shutdown`, and `settle`.

- [ ] **Step 1: Write failing Executor examples**

  Require accepted post, full rejection, FIFO start, serial busy behavior,
  finish-at-most-once behavior, shutdown rejection, and preservation proofs.

- [ ] **Step 2: Verify RED**

  ```powershell
  lake env lean Test/PhaseATests/IOExecutor.lean
  ```

  Expected: missing `CMetaCFlowCalculus.Proofs.IOExecutor`.

- [ ] **Step 3: Implement the minimum generic Executor transition system**

  Use monotonic nonzero task IDs, bounded queued tasks, positive execution
  parallelism, unique queued/running IDs, and a completed-ID observation trace.
  A rejected transition returns its input state unchanged.

- [ ] **Step 4: Prove Executor safety**

  Prove preservation for post/start/finish/shutdown/settle, accepted post
  appends exactly once, start removes exactly the FIFO head, finish removes one
  running task and records it once, and serial mode never has two running tasks.

- [ ] **Step 5: Run focused and aggregate tests**

  ```powershell
  lake env lean Test/PhaseATests/IOExecutor.lean
  lake test
  ```

- [ ] **Step 6: Commit the Executor model**

  ```powershell
  git add formal/cmeta_cflow_calculus
  git commit -m "formal(io): prove bounded executor transitions"
  ```

### Task 3: Define IO Actor request ownership and completion credit

**Files:**

- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/IOActor.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/Actor.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/IOActor.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**

- Consumes: `IO.BoundedMpsc.State Command` and
  `IO.Executor.State RequestId`.
- Produces: request admission, command consumption, cancel, backend completion,
  dispatch, acknowledgement, close, and quiescence transitions.

- [ ] **Step 1: Write failing Actor examples**

  Cover transactional submit, request-full and command-full rejection,
  admitted-to-ready processing, ready-to-pending submission,
  cancel-before-backend completion, cancel-request-after-submit, one backend
  terminal result, Executor-full preservation, accepted dispatch,
  acknowledgement release, close cancellation, and close rejection.

- [ ] **Step 2: Verify RED**

  ```powershell
  lake env lean Test/PhaseATests/IOActor.lean
  ```

  Expected: missing `CMetaCFlowCalculus.Proofs.IOActor`.

- [ ] **Step 3: Implement the minimum Actor model**

  Represent each live request as `{ id, lease, phase }`. Allocate monotonic
  nonzero IDs. Keep result data in `completed`; dispatch posts only the request
  ID to Executor. On Executor `full`, preserve the Actor and Executor states.

- [ ] **Step 4: Prove Actor safety**

  Prove capacity and uniqueness preservation, failed-submit transactional
  ownership, accepted submit adds one request and one command, request slots
  imply completion credit, stale completion leaves terminal state unchanged,
  dispatch-full preserves completion, acknowledgement releases exactly one
  request, and close rejects admission.

- [ ] **Step 5: Run focused and aggregate tests**

  ```powershell
  lake env lean Test/PhaseATests/IOActor.lean
  lake test
  ```

- [ ] **Step 6: Commit the Actor model**

  ```powershell
  git add formal/cmeta_cflow_calculus
  git commit -m "formal(io): prove actor request ownership"
  ```

### Task 4: Integrate aggregate imports and verify the phase

**Files:**

- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`
- Modify: `docs/superpowers/specs/2026-08-24-io-executor-lean-protocol.md`
  only if implementation evidence requires a contract correction.

**Interfaces:**

- Consumes: all Task 1-3 modules.
- Produces: library-visible models and proof modules plus aggregate test
  coverage.

- [ ] **Step 1: Add production and proof aggregate imports**

  Import all six modules from `CMetaCFlowCalculus.lean`, and all three test
  modules from `PhaseATests.lean`.

- [ ] **Step 2: Run fresh complete verification**

  ```powershell
  lake clean
  lake build
  lake test
  ```

- [ ] **Step 3: Inspect repository consistency**

  ```powershell
  git diff --check
  git status --short
  codegraph sync .
  codegraph affected -p . formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/BoundedMpsc.lean formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/Executor.lean formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/Actor.lean
  ```

- [ ] **Step 4: Commit integration-only changes if any**

  ```powershell
  git add docs/superpowers formal/cmeta_cflow_calculus
  git commit -m "docs(io): record executor actor proof protocol"
  ```

## Self-review

- Spec coverage: boundedness, ownership transfer, completion credit, Executor
  backpressure, cancellation, close, and deferred backend refinement each map
  to an explicit task.
- Placeholder scan: the plan contains no deferred implementation placeholder;
  explicitly deferred Reactor/Proactor/Disruptor work is outside this phase.
- Type consistency: `RequestId` is the Executor payload used by Actor dispatch;
  `BoundedMpsc.State Command` is the Actor command queue in every task.
