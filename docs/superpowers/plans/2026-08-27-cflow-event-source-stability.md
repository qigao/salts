# CFlow Event/Source stability implementation plan

> Design: `docs/superpowers/specs/2026-08-27-cflow-event-source-stability-design.md`

**Goal:** Close the remaining ownership, cancellation, and observability gaps
in base CFlow Source adapters without breaking existing successful callers.

**Architecture:** Preserve the Source and Channel facades. Constructors become
transactional, Channel gains an additive exact admission API, Timer owns
accepted callback lifetime explicitly, and readiness drivers provide a
quiescent cancellation boundary.

**Toolchain:** C11, CMake user presets, TinyTest, CTest, MSVC/ASan.

### Task 1: Constructor and Channel contracts

**Files:** `cflow/include/cflow/sources.h`, `cflow/src/sources.c`,
`cflow/tests/cflow_runtime_test.c`, `cflow/tests/cflow_temporal_test.c`.

1. Add occupied-output and occupied-Channel tests; observe failures.
2. Add exact Channel admission/status tests and statistics tests; observe the
   missing API at compile time.
3. Reject occupied destinations before allocation or mutation.
4. Implement exact push and stats under the existing Channel mutex; retain the
   bool wrapper.
5. Re-run focused tests.

### Task 2: Timer callback quiescence

**Files:** `cflow/src/sources.c`, `cflow/tests/cflow_runtime_test.c`.

1. Add a controllable foreign Scheduler regression test that races Timer
   cancellation/destruction with an executing callback.
2. Observe the old lifetime violation under ASan or deterministic test gates.
3. Add retained posting/callback references, callback-in-flight tracking, and
   reentrant-safe wake quiescence.
4. Re-run the Timer and runtime tests under Release and ASan.

### Task 3: Generic readiness boundary

**Files:** `cflow/include/cflow/sources.h`, `cflow/src/sources.c`,
`cflow/tests/cflow_runtime_test.c`.

1. Add tests for missing cancel, occupied output, and cancel-before-close order.
2. Require cancel during construction and document the quiescent contract.
3. Update repository callers to provide a no-op only where the fake driver
   provably never retains a waker.
4. Re-run runtime and readiness tests.

### Task 4: Verification and delivery

1. Build and run affected Release targets.
2. Run all `^cflow_` tests.
3. Configure/build the ASan preset and run affected CFlow tests.
4. Review the public header, diff, ownership paths, and test mutation cases.
5. Commit the isolated branch with verification evidence; do not push or merge
   unless separately requested.
