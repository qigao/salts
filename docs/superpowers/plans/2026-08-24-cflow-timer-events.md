# CFlow Monotonic Timer Events Implementation Plan

> Execute each task with RED -> GREEN -> focused regression -> commit.

**Goal:** Deliver monotonic TimerQueue readiness as bounded typed Events through
the existing Machine Mailbox path, with exact ownership and terminal accounting.

**Architecture:** A public opaque Timer Event Queue borrows a Clock and Machine
instance, owns fixed TimerQueue/payload storage, and has one `run_one_ready`
consumer. Fire claims one timer under the queue mutex, releases the mutex, calls
the existing non-blocking Machine Mailbox admission API exactly once, then
records the terminal result. No scheduler or Machine execution path is added.

**Tech stack:** C11, CMeta descriptors, CFlow Clock/TimerQueue/Mailbox/Machine,
Salts Platform synchronization, TinyTest, CMake Presets, Lean 4.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-timer-events-design.md`

## Task 1: Lock the public contract with failing tests

**Files:**

- Create: `cflow/tests/cflow_timer_event_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

1. Add compile/runtime tests for init, exact boundary, typed admission, and
   public C++ inclusion.
2. Configure/build `cflow_timer_event_test` and record RED because the header
   and symbols do not exist.
3. Commit the RED test contract only after observing the expected failure.

## Task 2: Add bounded typed scheduling and exact firing

**Files:**

- Create: `cflow/include/cflow/timer_event.h`
- Create: `cflow/src/timer_event.c`
- Create: `cflow/src/machine_runtime_internal.h`
- Modify: `cflow/src/machine_runtime.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/cflow_timer_event_test.c`

1. Add failing tests for invalid/type-mismatched Events, Timer full, deadline
   saturation, equal-deadline FIFO, and full Mailbox.
2. Add an internal read-only Machine Event contract query; do not expose Machine
   implementation state publicly.
3. Implement checked fixed storage, payload copying, TimerQueue schedule order,
   one-ready claim, exact Mailbox result, and statistics.
4. Build and run Timer Event, TimerQueue, Mailbox, and Machine runtime tests.
5. Commit the first GREEN functional slice.

## Task 3: Complete cancellation, close, and race semantics

**Files:**

- Modify: `cflow/src/timer_event.c`
- Modify: `cflow/tests/cflow_timer_event_test.c`
- Optionally create: `cflow/src/timer_event_internal.h` for deterministic test
  control over the claim/commit boundary.

1. Add failing tests for cancel-before-fire, cancel-during-handoff, repeated
   run, close with pending work, close during handoff, and accounting identity.
2. Implement mutex-linearized `PENDING -> FIRING`, `FIRE_WON`, idempotent close,
   close wait for in-flight handoff, and exact terminal counters.
3. Confirm no Clock or Machine operation occurs under the Timer Event mutex.
4. Run the focused suite repeatedly and under the repository sanitizer preset.
5. Commit the lifecycle slice.

## Task 4: Prove timer-to-Event trace refinement

**Files:**

- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/TimerEvent.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/TimerEvent.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/TimerEvent.lean`
- Modify formal aggregate imports as required by the existing project layout.

1. Add failing imports/examples for the absent Timer Event model.
2. Model ordered pending timers, cancel/fire terminal choice, and Mailbox send.
3. Prove boundedness, equal-deadline stable choice, append-once delivery, no
   delivery after cancel wins, and composition with Machine runtime traces.
4. Run focused Lean compilation, `lake build`, and `lake test`.
5. Commit the formal slice.

## Task 5: Documentation, verification, review, and PR

**Files:**

- Modify: public API documentation and completion notes where relevant.

1. Run fresh Windows Release configure/build and all `^cflow_` tests.
2. Run focused AddressSanitizer tests and Timer Event repeated execution.
3. Run Lean build/test and generated-surface checks.
4. Run `git diff --check`, CodeGraph sync/affected analysis, and review every
   validated HIGH/MED finding through a failing regression test.
5. Commit remaining synchronization changes, push `feat/cflow-timer-events`,
   and create a PR that closes #65 with exact verification evidence.
