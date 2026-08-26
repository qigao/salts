# CFlow execution lifecycle hardening implementation plan

> Design: `docs/superpowers/specs/2026-08-26-cflow-execution-lifecycle-hardening-design.md`

**Goal:** Ensure every accepted built-in CFlow task reaches one terminal state,
and reject destructive reinitialization or self-deadlocking synchronous joins.

**Architecture:** Preserve public facades. Repository-owned Scheduler backends
gain a private descriptor path backed by the existing Executor terminal-task
protocol. Runtime and parallel reduce consume that path and keep their current
public signatures.

**Toolchain:** C11, CMake user presets, TinyTest, CTest, MSVC/ASan.

### Task 1: Owning-handle guards

**Files:** `cflow/src/scheduler.c`, `cflow/src/scheduler_worker.c`,
`cflow/src/runtime.c`, `cflow/tests/cflow_execution_test.c`,
`cflow/tests/cflow_runtime_test.c`.

1. Add tests that second init/open fails and the first owner still works.
2. Run focused tests and observe failure.
3. Check live handles before mutation and publish only on success.
4. Re-run focused tests.

### Task 2: Scheduler terminal settlement

**Files:** `cflow/src/timer_queue.[ch]`, `cflow/src/scheduler_internal.h`,
`cflow/src/scheduler.c`, `cflow/src/scheduler_worker.c`,
`cflow/src/runtime.c`, related execution/runtime tests.

1. Add behavior tests for descriptor cancel/shutdown and Run close after
   Scheduler shutdown; observe failure or timeout.
2. Store copied descriptors in timer entries.
3. Add private built-in descriptor admission.
4. Settle cancelled Scheduler-owned work outside backend locks.
5. Make Run pump cancellation terminate and release its accepted reference.
6. Re-run focused tests.

### Task 3: Parallel reduce settlement and context guard

**Files:** `cflow/src/executor_internal.h`, `cflow/src/executor.c`,
`cflow/src/plan_parallel_reduce.c`, `cflow/tests/cflow_direct_test.c`.

1. Add cancel-pending and same-single-worker regression tests; observe failure
   or bounded test timeout.
2. Post built-in task descriptors with a cancellation settlement callback.
3. Reject synchronous parallel reduce from the same built-in Executor callback.
4. Re-run focused tests.

### Task 4: Verification and delivery

1. Configure/build through `win-release-user` and run focused targets.
2. Run all `^cflow_` tests.
3. Configure/build `win-dev-user` and run affected CFlow tests under ASan.
4. Review `git diff`, public headers, ownership paths, and test mutation cases.
5. Commit, push `fix/cflow-execution-lifecycle`, and open a GitHub PR against
   `master` with evidence and compatibility notes.
