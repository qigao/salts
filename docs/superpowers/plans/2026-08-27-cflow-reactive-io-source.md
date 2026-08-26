# CFlow Reactive I/O Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded Actor-to-Source adapter so CFlow downstream demand controls asynchronous I/O submission and authoritative completion resumes the Run.

**Architecture:** A new `cflow/io_source.h` adapter owns one `cflow_io_actor`, one capacity-one manual Executor, and one typed completion slot while borrowing the backend and user callbacks. Source demand prepares at most one move-only operation; owner driving converts Actor completion into a copied trivial CMeta value and wakes the Run. Source and owner share lifecycle state so cancellation can drain without blocking `cflow_run_close()`.

**Tech Stack:** C11, CMeta type descriptors, CFlow Source/Waitable/Run, CFlow I/O Actor and Executor, Turbo mutexes, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-27-cflow-reactive-io-source-design.md`

## Global Constraints

- Preserve all existing `cflow_io_actor`, native backend, file facade, readiness Source, Runtime, Graph, and package behavior.
- Keep one in-flight request and one copied completion value; do not add fallback queues, polling threads, or unbounded allocation.
- Only trivial-copy/trivial-destroy CMeta output types are accepted.
- Actor request phase remains the I/O fact source; the adapter completion slot is only the authoritative encoded output.
- No user callback, backend callback, source waker, or driver wake runs while the adapter gate is held.
- Successful prepare transfers the operation token to the adapter; every rejection releases it exactly once.
- Source destruction requests close but does not destroy borrowed backend state; owner close requires Source destruction and Actor quiescence.
- Use `win-release-user` through `VsDevCmd.bat`; run the smallest test first, then adjacent and full regression.

---

### Task 1: Lock the additive public contract

**Files:**
- Create: `cflow/tests/cflow_io_source_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Create after RED: `cflow/include/cflow/io_source.h`
- Modify after RED: `cflow/include/cflow/cflow.h`

**Interfaces:**
- Consumes: `cflow_source`, `cflow_io_operation`, `cflow_io_backend_ops`, `cflow_read_status`.
- Produces: the exact types and functions declared in the design spec.

- [ ] **Step 1: Write the failing public-contract test**

  Add a TinyTest executable that includes `<cflow/cflow.h>` and declares zero-state
  `cflow_io_source_owner`, `cflow_io_source_config`, and `cflow_io_source_stats`.
  Assert invalid construction leaves both outputs zero:

  ```c
  spec("CFlow reactive IO source") {
      it("rejects an empty configuration without mutating outputs") {
          cflow_source source = {0};
          cflow_io_source_owner owner = {0};
          cflow_io_source_config config = {0};

          check_equal(cflow_source_from_io_actor(
                          &source, &owner, &config), TURBO_EINVAL);
          check_false(cflow_source_valid(&source));
          check_null(owner.impl);
      }
  }
  ```

- [ ] **Step 2: Register and run RED**

  Register with the existing helper:

  ```cmake
  cmake_add_test(
    SOURCES cflow_io_source_test.c
    LIBS turbo_cflow tinytest
    FOLDER "cflow/tests")
  ```

  Reconfigure with `cmake --fresh --preset win-release-user`, then build target
  `cflow_io_source_test`. Expected: compilation fails because the new public names do not exist.

- [ ] **Step 3: Add the minimal complete header contract**

  Create `io_source.h` with the exact declarations from the spec, complete parameter,
  ownership, thread, error, and shutdown documentation. Include it from `cflow.h`.
  Add no function bodies or placeholder returns.

- [ ] **Step 4: Verify the failure advances to missing implementation**

  Rebuild `cflow_io_source_test`. Expected: compilation succeeds and linking fails on
  `cflow_source_from_io_actor`, proving the test consumes the intended public API.

- [ ] **Step 5: Commit the contract test and header**

  ```text
  test(cflow): define reactive IO source contract
  ```

### Task 2: Implement construction, zero-demand, and terminal preparation

**Files:**
- Create: `cflow/src/io_source.c`
- Modify: `cflow/tests/cflow_io_source_test.c`

**Interfaces:**
- Consumes: the Task 1 public contract, `cflow_value_slot`, manual Executor, I/O Actor.
- Produces: validated constructor, Source interface, owner stats/close for a never-submitted Source.

- [ ] **Step 1: Add RED tests for construction and no-demand behavior**

  Implement a complete fake backend fixture with literal counters and a release callback.
  Create an identity graph, deterministic scheduler, Run, and collecting sink. Assert:

  ```c
  check_true(cflow_run_open(&run, &graph, &source, &scheduler, &sink));
  check_equal(fixture.prepare_calls, (size_t)0u);
  check_equal(fixture.backend_submit_calls, (size_t)0u);
  check_equal(cflow_scheduler_run_until_idle(&scheduler, 0u), (size_t)0u);
  check_equal(fixture.prepare_calls, (size_t)0u);
  ```

  Add separate prepare-DONE and prepare-ERROR cases. DONE must notify the sink exactly once;
  ERROR must expose the fixture's stable literal error and create no Actor request.

- [ ] **Step 2: Run RED**

  Build and execute `cflow_io_source_test --filter "without downstream demand"` and the two
  preparation filters. Expected: link failure before implementation, then behavioral failure
  until Source resume and terminal mapping exist.

- [ ] **Step 3: Implement the minimal shared state and constructor**

  In `io_source.c`, define a private shared state with:

  ```c
  cflow_io_actor actor;
  cflow_executor executor;
  cflow_value_slot result;
  turbo_mutex_t gate;
  cflow_waker source_waker;
  cflow_io_request_id request_id;
  bool source_live;
  bool owner_live;
  bool close_requested;
  bool driver_active;
  bool result_ready;
  bool acknowledged;
  ```

  Validate zero-state outputs, callbacks, backend submit function, descriptor traits,
  type size/alignment, and exact callback dependencies before allocation. Initialize a
  capacity-one manual Executor and request/command-capacity-one Actor. On any failure,
  unwind each initialized resource once and leave outputs zero.

- [ ] **Step 4: Implement Source prepare/terminal behavior**

  Source resume invokes `prepare` only from idle state and only when Runtime calls resume.
  DONE returns `CFLOW_STEP_DONE`; ERROR returns `CFLOW_STEP_ERROR`; OPERATION validates the
  move token and submits it. Source cancel/ destroy close Actor admission, clear wakers, and
  release only the Source reference. Owner close succeeds only for a destroyed, quiescent Source.

- [ ] **Step 5: Run GREEN and adjacent Runtime test**

  Run `cflow_io_source_test`, then `cflow_runtime_test`. Expected: all cases pass without
  warnings or leaked live handles.

- [ ] **Step 6: Commit constructor and terminal behavior**

  ```text
  feat(cflow): add demand-gated IO source adapter
  ```

### Task 3: Bridge Actor completion to WAIT/wake/value

**Files:**
- Modify: `cflow/src/io_source.c`
- Modify: `cflow/tests/cflow_io_source_test.c`

**Interfaces:**
- Consumes: Task 2 Source state and fake backend.
- Produces: `owner_run_ready`, completion encoding, automatic acknowledge, lost-wake-safe Waitable.

- [ ] **Step 1: Write the basic demand-to-value RED test**

  Request one downstream value and assert the literal sequence:

  ```c
  check_true(cflow_run_request(&run, 1u));
  check_equal(cflow_scheduler_run_until_idle(&scheduler, 0u), (size_t)1u);
  check_equal(fixture.prepare_calls, (size_t)1u);
  check_equal(fixture.backend_submit_calls, (size_t)0u);
  check_true(fixture.drive_wakes >= (size_t)1u);

  check_equal(cflow_io_source_owner_run_ready(
                  &owner, 32u, &progressed), TURBO_OK);
  check_true(progressed > (size_t)0u);
  check_equal(cflow_scheduler_run_until_idle(&scheduler, 0u), (size_t)1u);
  check_equal(sink.values[0], 37);
  check_equal(fixture.releases, (size_t)1u);
  ```

  The fake backend completes through the real `cflow_io_actor_complete()` API; assertions target
  emitted value, request count, release count, and terminal state rather than mock invocation alone.

- [ ] **Step 2: Run RED and confirm the missing bridge**

  Expected failure: Run remains waiting and the sink receives zero values because completion has
  not yet been encoded and woken into the Source.

- [ ] **Step 3: Implement completion delivery and acknowledge**

  The Actor completion callback reserves the single result slot under the gate, invokes encoder
  outside the gate, publishes VALUE/VALUE_AND_DONE/DONE/ERROR under the gate, marks delivery,
  and invokes the captured Source waker outside the gate. `owner_run_ready()` follows the proven
  `cflow_io_file_run_ready()` order: acknowledge delivered request, drive one Actor transition,
  run one Executor task, repeat up to `max_steps`.

- [ ] **Step 4: Implement WAIT arm/cancel without lost wake**

  Waitable arm installs the waker under the gate. If result or acknowledgement became ready before
  arm, it invokes the waker immediately after unlocking. Waitable cancel clears the exact stored
  waker and requests Actor close/cancel without waiting for native completion.

- [ ] **Step 5: Add and pass completion-before-arm RED/GREEN test**

  Configure `drive` to synchronously call `owner_run_ready()` from the Actor wake edge. The backend
  completes synchronously during submit. Assert one value, one acknowledge/release, no duplicate
  preparation, and Run completion. This catches removal of the arm-time readiness recheck.

- [ ] **Step 6: Commit the completion bridge**

  ```text
  feat(cflow): wake reactive runs from IO completion
  ```

### Task 4: Prove backpressure, cancellation, and shutdown

**Files:**
- Modify: `cflow/src/io_source.c`
- Modify: `cflow/tests/cflow_io_source_test.c`

**Interfaces:**
- Consumes: complete Task 3 adapter.
- Produces: bounded sequential demand, race-safe cancellation, observable stats and retryable close.

- [ ] **Step 1: Write RED tests for sequential demand and inline reentry**

  Request two values at once. Arrange the scheduler to run inline from Source wake. Assert the
  second prepare does not occur until the first request is acknowledged, maximum backend-active
  count is exactly one, values are `[11, 29]`, and releases equal accepted operations.

- [ ] **Step 2: Implement acknowledge gating**

  Keep request identity active until `cflow_io_actor_acknowledge()` returns RELEASED. If a value was
  consumed before acknowledge, subsequent resume returns WAIT; successful acknowledge transitions
  to idle and wakes the Source. Never convert Actor FULL into retry or allocation.

- [ ] **Step 3: Write RED tests for cancellation and early owner close**

  Cover cancellation before backend submit and after backend submit. Assert owner close returns
  `TURBO_EBUSY` while Source is live or a native request is pending; completion CANCELLED is drained,
  operation release runs exactly once, owner eventually becomes quiescent, close clears owner, and
  backend/context remain caller-owned.

- [ ] **Step 4: Implement close/drain and stats**

  Source cancel atomically closes admission, clears Source waker, and delegates to Actor close.
  Completion after cancellation skips encoder but still becomes delivered and acknowledged.
  `get_stats()` copies Actor stats plus Source state under the gate. Concurrent/reentrant
  `owner_run_ready()` returns `TURBO_EBUSY` and reports zero progress.

- [ ] **Step 5: Add malformed encoder and operation tests**

  Verify prepare OPERATION with an empty token is released/rejected, encoder WOULD_BLOCK becomes a
  stable protocol error, duplicate backend completion remains Actor-stale rather than a second item,
  and VALUE_AND_DONE emits exactly one final value.

- [ ] **Step 6: Run the complete focused suite and commit**

  Run `cflow_io_source_test`, `cflow_io_actor_test`, `cflow_runtime_test`, and
  `cflow_readiness_test`.

  ```text
  test(cflow): prove reactive IO lifecycle
  ```

### Task 5: Package, documentation, and regression verification

**Files:**
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/plans/2026-08-27-cflow-reactive-io-source.md`

**Interfaces:**
- Consumes: finished public API and implementation.
- Produces: installed-header compatibility, runnable example documentation, reproducible evidence.

- [ ] **Step 1: Add C++17 aggregate-header coverage**

  Add `static_assert` checks for prepare/encode callback signatures and compile a zero-state owner,
  config, and stats object through `<cflow/cflow.h>`.

- [ ] **Step 2: Document the two I/O entry points**

  Add a decision table to `cflow/README.md`:

  ```text
  readiness resource -> cflow_source_from_reactor_registration
  authoritative completion operation -> cflow_source_from_io_actor
  direct multi-request/manual lifecycle -> cflow_io_actor / cflow_io_file
  ```

  Include a complete prepare/encode/drive/Run/close example and explicitly state buffer and backend
  lifetimes, capacity one, no-demand behavior, and shutdown order.

- [ ] **Step 3: Run focused and adjacent Release tests**

  Build the CFlow target and run:

  ```text
  cflow_io_source_test
  cflow_io_actor_test
  cflow_io_native_test
  cflow_io_file_test
  cflow_runtime_test
  cflow_readiness_test
  cflow_header_cpp_test
  cflow_pipeline_test
  cflow_graph_test
  ```

- [ ] **Step 4: Run full Release and install verification**

  Execute `ctest --preset win-release-user --output-on-failure`, then
  `cmake --build --preset install-win-release-user`. Confirm the installed SDK contains
  `include/cflow/io_source.h` and the existing installed-package consumer remains green.

- [ ] **Step 5: Run available sanitizer/platform coverage**

  Configure/build/test the focused target with `win-dev-user`. If the environment cannot run ASan
  or another native backend, record the exact command, error, and residual platform risk rather than
  substituting a fallback backend.

- [ ] **Step 6: Final structural and patch verification**

  Run `codegraph sync .`, `codegraph affected` for the new header/source/test, `git diff --check`,
  and clean-worktree status. Review the public ownership comments against the design spec and verify
  every accepted operation has exactly one release path.

- [ ] **Step 7: Commit the verified documentation/package surface**

  ```text
  docs(cflow): document reactive IO source adapter
  ```

