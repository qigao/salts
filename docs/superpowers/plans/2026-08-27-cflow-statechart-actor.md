# Statechart Actor Facade Implementation Plan

> Work inline in this branch and keep every step independently verifiable.

**Goal:** Add a bounded Statechart-backed facade to the existing CFlow Actor
lifecycle without making Graph own Statechart semantics or changing Machine
Actor behavior.

**Architecture:** Refactor `cflow_actor_impl` around one private runtime
strategy selected at initialization. Both runtimes feed the existing identity
Run; the Statechart strategy uses the terminal-only Source and treats natural
completion as successful Actor termination.

**Stack:** C11, CMeta typed Events, CFlow Statechart Runtime/Run/Scheduler,
TurboUtils threading, TinyTest, CMake presets.

---

### Task 1: Specify the public contract with failing tests

**Files:**
- Create: `cflow/tests/cflow_statechart_actor_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

Add compile/runtime tests for initialization, normal final completion,
admission, stats, stop/error, and stale references. Build the focused target
and retain the missing-symbol/type failure as RED evidence.

### Task 2: Add the Statechart Actor public surface

**Files:**
- Modify: `cflow/include/cflow/actor.h`

Add Statechart config/result/stats declarations and initializer/getter docs.
Append the Statechart rejection status without renumbering existing statuses.

### Task 3: Introduce the private runtime strategy

**Files:**
- Modify: `cflow/src/actor.c`

Move Machine-specific attach/send/close/cancel/destroy behind a static
strategy. Add the corresponding Statechart operations, shared initialization,
runtime-specific done policy, and stats dispatch. Keep callbacks outside the
Actor gate and preserve first-error ownership.

### Task 4: Document the facade and boundaries

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/plans/2026-08-27-cflow-statechart-phase1.md`

Document ownership, two-executor roles, zero-value terminal projection,
capacity/backpressure, natural completion, shutdown order, and exclusions.

### Task 5: Verify behavior and packaging

Run the focused Statechart Actor test, existing Actor/Statechart adapter tests,
all CFlow tests, focused ASan, C++ header test, and installed-package consumer
matrix. Inspect the final diff and CodeGraph affected set before completion.

## Verification evidence

- RED: the focused target failed to compile on the absent Statechart Actor
  config/result/stats types, rejection status, initializer, and stats getter.
- Windows Release: the Statechart Actor test passed five consecutive runs;
  existing Machine Actor, terminal adapter, and C++ public-header tests passed
  4/4. The installed-package smoke target built all 18 consumer steps and its
  CFlow consumer referenced both new exported symbols.
- Windows ASan: the same four focused tests passed 4/4 after loading the Visual
  Studio sanitizer runtime; the Statechart Actor test separately passed five
  consecutive ASan runs.
- All CFlow: the final Release run passed 33/33. A separate repeated stress run
  of the unrelated existing `cflow_execution_test` timer-handoff counter test
  passed twice and failed on its third run at two pre-existing timing
  assertions; this change does not modify Scheduler, Executor, or Timer
  implementation files.
