# CFlow Temporal Source Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add bounded monotonic `delay`, `debounce`, and `timeout` Source adapters that preserve explicit downstream demand, cancellation, and Source terminal behavior.

**Architecture:** Each adapter moves one inner `cflow_source`, retains at most one typed value, and schedules at most one task on the existing run scheduler. A mutex protects wake races; callbacks only publish readiness and invoke a copied waker after unlocking.

**Tech Stack:** C11, CMeta lifecycle traits, CFlow Source/Scheduler/Waitable, Salts mutexes, TinyTest, CMake Presets, Lean 4.

---

### Task 1: Specify temporal contract with failing tests

**Files:**
- Create: `cflow/tests/cflow_temporal_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

1. Add virtual-scheduler TinyTest fixtures for delay timing, debounce replacement/final flush, timeout failure, zero duration, simultaneous readiness, and downstream demand.
2. Add managed-value lifecycle counters and cancellation/close cases.
3. Register `cflow_temporal_test` and build it to confirm failure because the API is absent.

### Task 2: Implement one-slot temporal Source state machines

**Files:**
- Create: `cflow/include/cflow/temporal.h`
- Create: `cflow/src/temporal.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

1. Implement move-style construction with validation and one preallocated lifecycle-aware value slot.
2. Implement shared timer/waker state that schedules through `cflow_resume_ctx.scheduler`, forwards inner wakers without retaining adapter state, and never invokes a callback while locked.
3. Implement delay as pull-one/hold-one/emit-on-deadline.
4. Implement debounce as replace-one/reset-deadline/emit-on-quiet, with immediate final flush on upstream completion.
5. Implement timeout as arm-only-on-WAIT and fail when the deadline is observed while upstream still returns WAIT.
6. Compose terminal polling/wakers and make cancel/destroy idempotent with exactly-once inner destruction.
7. Run the temporal, runtime, scheduler compatibility, and C++ header tests.

### Task 3: Formalize temporal states and legal rewrites

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Temporal.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Temporal.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/Temporal.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

1. Model monotonic instants, one-slot retained state, and timer/upstream winner states.
2. Prove retained cardinality is at most one and deadlines never move backward.
3. Prove debounce replacement retains the newest value and timeout maps an observed timer cause to its unique error result.
4. State and prove only semantics-preserving rewrite laws, including zero-delay identity at the current scheduler instant.
5. Run `lake test`.

### Task 4: Verification and delivery

**Files:**
- Modify only files required by compiler/test findings.

1. Build the focused CFlow targets in Release and run hierarchy, temporal, Machine, runtime, Actor, Timer Event, and C++ header tests.
2. Run the broader CFlow CTest label/directory suite and `lake test`.
3. Run the hierarchy benchmark and capture its actual output.
4. Inspect `git diff --check`, `git status`, public-header installation coverage, and absence of `.codegraph` in the index.
5. Commit the complete issue with an issue-referencing message, push the feature branch, and create a PR whose body lists contract, tests, benchmark output, compatibility, and residual risk.
