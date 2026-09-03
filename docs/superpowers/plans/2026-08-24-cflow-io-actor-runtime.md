# CFlow IO Actor Runtime Implementation Plan

> **Execution:** Inline in the current isolated worktree, because the user has
> already requested implementation and the tasks share one evolving public API.

**Goal:** Implement the platform-neutral C runtime refinement of the proved
bounded IO Actor/Executor protocol.

**Architecture:** A fixed request table owns operations and completion credits;
a single-consumer Disruptor mailbox accepts concurrent submit/cancel commands;
backend strategy callbacks cover Reactor and Proactor models; existing
`cflow_executor` performs completion delivery.

**Tech Stack:** C11, Salts Disruptor/thread primitives/error codes,
CFlow Executor, TinyTest, CMake presets, Lean 4 conformance model.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-io-actor-runtime-design.md`

### Task 1: Freeze the public protocol with RED tests

**Files:**
- Create: `cflow/tests/cflow_io_actor_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Create: `cflow/include/cflow/io_actor.h`

1. Add compile-time and behavior tests for zero-state init validation, move-only
   submit, request/command capacity, lease uniqueness and closed admission.
2. Configure/build `cflow_io_actor_test`; verify RED because the implementation
   symbols do not exist.

### Task 2: Implement bounded admission and backend driving

**Files:**
- Create: `cflow/src/io_actor.c`
- Modify: `cflow/CMakeLists.txt`

1. Allocate fixed request slots and a one-consumer Disruptor mailbox at init.
2. Implement transactional `try_submit`, `try_cancel`, FIFO `run_one` and
   bounded `run_ready`.
3. Add fake backend tests for sync submit failure, early cancel, pending cancel,
   FIFO observation and stale completion.

### Task 3: Implement Executor delivery and completion acknowledgement

1. Store terminal completion in the reserved request slot.
2. Post a stable request slot to the injected Executor; retain completion on
   full/closed and retry later.
3. Mark queued/running/delivered around the user callback, and release the
   move-only operation only after successful acknowledgement.
4. Test manual, serial and saturated Executor paths plus pre-delivery ack
   rejection.

### Task 4: Implement close, quiescence and observability

1. Close command admission once, cancel admitted/ready requests and request
   cancellation for backend-pending work.
2. Add stats derived from the request table and exact rejection/error counters.
3. Make destroy fail with `SALTS_EBUSY` until commands, requests, driver and
   delivery callbacks are quiescent.
4. Test close drain, unacknowledged completion, pending backend and terminal
   accounting.

### Task 5: Verify conformance and regressions

1. Run focused `cflow_io_actor_test` after every GREEN step.
2. Run `cflow_execution_test`, `cflow_actor_test`, `cflow_readiness_test`,
   `disruptor_test` and C++ header compatibility.
3. Run Windows Release and dev/ASan relevant tests.
4. Run sequential `lake clean`, `lake build`, `lake test`.
5. Run `git diff --check`, CodeGraph sync/affected and inspect final status.
