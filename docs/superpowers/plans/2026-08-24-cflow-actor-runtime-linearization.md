# CFlow Actor Runtime Linearization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Machine cancellation and transition commit linearizable while preserving the existing Actor, Graph, Run, Mailbox, and hierarchy public behavior.

**Architecture:** Keep the Machine instance as the sole mutable state owner and the SerialExecutor as its sole semantic consumer. Add orthogonal lifecycle/worker phases and arbitrate cancel versus commit under the existing instance mutex; Graph remains a typed output topology and receives the Machine only through its Source adapter.

**Tech Stack:** C11, Salts mutex/executor primitives, CFlow Machine/Mailbox/Actor/Run, TinyTest, Lean 4

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-actor-runtime-linearization-design.md`

## Global Constraints

- Do not change public C signatures, struct layouts, error enums, Graph IR, or serialized formats.
- Do not execute guard/action callbacks, wakers, sink callbacks, allocation, or I/O while holding the Machine instance mutex.
- Mailbox capacity remains fixed; full/closed/cancelled outcomes remain explicit and no fallback or retry is added.
- The SerialExecutor remains the sole Machine transition consumer; no second worker, pump, scheduler, or inline execution path is added.
- Build and test through existing CMake presets; run Lean build and test sequentially because they share `.lake/build`.
- Linux implementation verification runs remotely through the repository's approved `root@eu` development path.

---

### Task 1: Add deterministic cancel/commit race tests

**Files:**
- Modify: `cflow/src/machine_runtime_internal.h`
- Modify: `cflow/src/machine_runtime.c`
- Modify: `cflow/tests/cflow_machine_runtime_test.c`

**Interfaces:**
- Consumes: existing `cflow_machine_instance_init_internal()` and transition commit hook.
- Produces: a test-only/internal commit-boundary hook capable of blocking immediately before arbitration, without changing installed headers.

- [x] **Step 1: Add a two-party barrier fixture to the Machine runtime test**

Define a fixture using repository thread/mutex/condition primitives. Its internal hook records entry at the transition boundary, waits until the test thread releases it, and never calls public Machine APIs while holding the fixture lock.

- [x] **Step 2: Write the cancel-wins test**

The test sends one Event, waits until the executor reaches the boundary, calls `cflow_machine_instance_cancel()` from the test thread, releases the executor, waits for quiescence, and asserts:

```c
check_equal(step.kind, CFLOW_STEP_DONE);
check_equal(copied_state, initial_state);
check_equal(stats.completed, (uint64_t)0u);
check_equal(stats.cancelled_events, (uint64_t)1u);
check_equal(stats.accepted,
            stats.completed + stats.failed + stats.cancelled_events);
check_equal(stats.in_flight, (size_t)0u);
```

- [x] **Step 3: Run the focused test and record RED**

Run:

```powershell
cmake --preset win-release-user
cmake --build --preset win-release-user --target cflow_machine_runtime_test
ctest --preset win-release-user -R '^cflow_machine_runtime_test$' --output-on-failure
```

The new cancel-wins case must fail on the copied state or completed counter before the runtime fix.

- [x] **Step 4: Write the commit-wins test harness path**

Let the executor cross the commit boundary before issuing cancel, then assert one target-state commit, at most one downstream VALUE, no processing of a second queued Event, and quiescent accounting identity.

- [x] **Step 5: Commit the RED tests**

Commit only the internal test hook and deterministic tests with message:

```text
test(cflow): expose machine cancel commit race
```

### Task 2: Implement mutex-linearized commit arbitration

**Files:**
- Modify: `cflow/src/machine_runtime.c`
- Modify: `cflow/src/machine_runtime_internal.h` only when the phase type is shared with an internal test hook
- Test: `cflow/tests/cflow_machine_runtime_test.c`

**Interfaces:**
- Consumes: the deterministic tests from Task 1 and existing public Machine runtime functions unchanged.
- Produces: internal `control_lifecycle` and `worker_phase` state plus one locked `begin_commit` decision.

- [x] **Step 1: Replace overlapping execution booleans with explicit internal phases**

Introduce internal enums equivalent to:

```c
typedef enum cflow_machine_control_lifecycle {
    CFLOW_MACHINE_CONTROL_OPEN = 0,
    CFLOW_MACHINE_CONTROL_CLOSE_REQUESTED,
    CFLOW_MACHINE_CONTROL_CANCEL_REQUESTED,
    CFLOW_MACHINE_CONTROL_TERMINAL
} cflow_machine_control_lifecycle;

typedef enum cflow_machine_worker_phase {
    CFLOW_MACHINE_WORKER_IDLE = 0,
    CFLOW_MACHINE_WORKER_SCHEDULED,
    CFLOW_MACHINE_WORKER_EXECUTING,
    CFLOW_MACHINE_WORKER_COMMITTING
} cflow_machine_worker_phase;
```

Keep compatibility booleans only where public statistics require them, derive them from the authoritative phase under the lock, and reject impossible transitions internally.

- [x] **Step 2: Add the locked commit decision**

After action completion and target validation, acquire the instance mutex once. If lifecycle is `CANCEL_REQUESTED`, clear staged readiness, decrement in-flight once, increment cancelled once, and leave source state untouched. Otherwise change worker phase from `EXECUTING` to `COMMITTING` and perform state ID/value, output readiness, and completion accounting in the same critical section.

- [x] **Step 3: Make cancel use the same phase fact source**

Under the instance mutex, stop admission first. For `EXECUTING`, publish `CANCEL_REQUESTED`; for `COMMITTING`, preserve the already-linearized commit and prevent subsequent work. Extract wakers under the lock and invoke them only after unlocking.

- [x] **Step 4: Preserve close semantics**

Close changes `OPEN` to `CLOSE_REQUESTED`. An executing or committing turn completes once; queued Events are cancelled; VALUE produces VALUE_AND_DONE before terminal settlement. Repeated close/cancel calls remain idempotent and cancellation dominates a prior close only before commit linearizes.

- [x] **Step 5: Run focused GREEN tests**

Run:

```powershell
cmake --build --preset win-release-user --target cflow_machine_runtime_test
ctest --preset win-release-user -R '^cflow_machine_runtime_test$' --output-on-failure
```

Both deterministic race cases, reentrant cancel, reentrant close, queued cancellation, and concurrent producer accounting must pass.

- [x] **Step 6: Commit the runtime fix**

Commit with message:

```text
fix(cflow): linearize machine cancel and commit
```

### Task 3: Propagate the protocol through Actor and hierarchy regressions

**Files:**
- Modify: `cflow/tests/cflow_actor_test.c` only if an Actor-level deterministic assertion is not already expressible through Machine tests
- Modify: `cflow/tests/cflow_machine_hierarchy_test.c` only for hierarchy-specific commit/timer ordering coverage
- Modify: `cflow/include/cflow/machine_runtime.h` documentation comments without changing declarations
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: the linearized Machine runtime from Task 2.
- Produces: public documentation of overlapping cancel semantics and adjacent regression evidence.

- [x] **Step 1: Document the linearization rule**

State that cancel winning before commit discards staged logical state/output, while commit winning first permits exactly one commit and then prevents later Events. Explicitly state that arbitrary external callback effects are not rolled back.

- [x] **Step 2: Add Actor failure/stop regression coverage**

Exercise Actor stop as close semantics and Actor failure as cancellation semantics. Assert exact lifecycle state, first error, Machine accounting identity, and no post-terminal values.

- [x] **Step 3: Add hierarchy timer ordering coverage**

For a scoped timer on an exited state, prove it is cancelled exactly once after the winning commit; for cancel-wins, prove the source state and active scope remain logically unchanged until terminal cleanup closes the timer queue.

- [x] **Step 4: Run adjacent CFlow tests**

Run:

```powershell
cmake --build --preset win-release-user --target cflow_machine_runtime_test cflow_actor_test cflow_machine_hierarchy_test cflow_timer_event_test cflow_event_mailbox_test cflow_runtime_test
ctest --preset win-release-user -R '^cflow_(machine_runtime|actor|machine_hierarchy|timer_event|event_mailbox|runtime)_test$' --output-on-failure
```

- [x] **Step 5: Commit the contract propagation**

Commit with message:

```text
docs(cflow): define overlapping machine cancellation
```

### Task 4: Verify the executable refinement and platform matrix

**Files:**
- Modify: implementation plan verification notes only when exact preset/CI evidence needs recording

**Interfaces:**
- Consumes: all prior tasks.
- Produces: fresh evidence suitable for PR review and merge.

- [x] **Step 1: Run Lean verification sequentially**

From `formal/cmeta_cflow_calculus`, run:

```powershell
lake build
lake test
rg.exe -n '\b(sorry|axiom|admit|unsafe)\b' CMetaCFlowCalculus/CFlow/MachineRuntime.lean CMetaCFlowCalculus/Proofs/MachineRuntime.lean Test/PhaseATests/MachineRuntime.lean
```

- [x] **Step 2: Run Windows verification**

Run the adjacent command from Task 3 with `win-release-user`, then run:

```powershell
cmake --preset win-dev-user
cmake --build --preset win-dev-user --target cflow_machine_runtime_test cflow_actor_test cflow_machine_hierarchy_test cflow_timer_event_test cflow_event_mailbox_test cflow_runtime_test
ctest --preset win-dev-user -R '^cflow_(machine_runtime|actor|machine_hierarchy|timer_event|event_mailbox|runtime)_test$' --output-on-failure
```

`win-dev-user` enables the repository's AddressSanitizer configuration. Record exact test counts, failures, and unsupported sanitizer limitations.

- [x] **Step 3: Run remote Linux verification**

On `root@eu`, fetch the exact commit, then run:

```bash
cmake --preset linux-dev-user
cmake --build --preset linux-dev-user --target cflow_machine_runtime_test cflow_actor_test cflow_machine_hierarchy_test cflow_timer_event_test cflow_event_mailbox_test cflow_runtime_test
ctest --preset linux-dev-user -R '^cflow_(machine_runtime|actor|machine_hierarchy|timer_event|event_mailbox|runtime)_test$' --repeat until-fail:20 --output-on-failure
```

Run additional UBSan/TSan jobs only through repository presets or CI jobs that explicitly enable them. An unavailable tool is reported as residual risk, not replaced by ordinary unit tests.

- [x] **Step 4: Review the final diff and affected graph**

Run `codegraph affected` for the changed runtime/header/test files, inspect `git diff --check`, confirm no public ABI or generated artifact drift, and verify the worktree contains only intended files.

- [x] **Step 5: Commit verification-only documentation if changed**

Use message:

```text
test(cflow): verify machine commit arbitration
```

### Verification evidence

- Lean: `lake build` completed 46 jobs, `lake test` exited successfully, and the proof sources contain no `sorry`, `axiom`, `admit`, or `unsafe` escape.
- Windows Release: the six adjacent CFlow tests passed 6/6 at `f2405776b1d51cef3de859aeca4b7290a08928c5`; the focused Machine runtime and Actor tests also passed 20 consecutive runs each.
- Windows AddressSanitizer: the same six-test matrix passed 6/6 after adding the installed MSVC ASan runtime directory to the test process `PATH`.
- Remote Linux AddressSanitizer: `root@eu` tested exact commit `f2405776b1d51cef3de859aeca4b7290a08928c5`; all six tests passed `--repeat until-fail:20`, for 120 successful process runs.
- CodeGraph: the refreshed affected-test set contains `cflow/tests/cflow_machine_runtime_test.c`; the adjacent Actor, hierarchy, timer, mailbox, and Runtime tests were retained as explicit protocol regressions.
