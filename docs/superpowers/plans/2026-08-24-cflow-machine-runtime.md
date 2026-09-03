# CFlow Machine Resumable Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute validated CFlow Machines through one borrowed SerialExecutor and the existing Resumable/Source demand protocol with bounded Event ownership and complete terminal accounting.

**Architecture:** A `cflow_machine_instance` owns copied mutable state, one bounded Mailbox, normalized callback bindings, and one prepared output. External producers only copy Events into the Mailbox; executor tasks alone select and commit transitions, while a Resumable/Source adapter maps readiness to VALUE/WAIT/DONE/ERROR without adding demand or scheduler state.

**Tech Stack:** C11, CMeta descriptors/interfaces, CFlow Machine/Mailbox/Executor/Resumable/Source, Salts Platform synchronization, TinyTest, CMake Presets, Lean 4.33.1 and Lake.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-machine-runtime-design.md`

## Global Constraints

- Existing Machine, Mailbox, Graph, Run, scheduler, executor, and Source behavior remains unchanged.
- Machine semantic state has one mutable owner: callbacks running through the borrowed non-manual SerialExecutor.
- No implicit worker, scheduler, demand model, unbounded queue, or inline fallback is allowed.
- Runtime accepts only ABI-safe trivially copyable/destructible state, Event, observation, and downstream value types.
- Close/cancel must reject new Events, detach waits, settle accepted Events, preserve the first error, and prevent stale callbacks.
- Runtime destroy requires producer/control-plane quiescence and occurs after adapter consumers close.
- Each production behavior is implemented only after its focused TinyTest or Lean test fails for the expected missing behavior.

---

### Task 1: Public runtime contract and transactional admission

**Files:**
- Create: `cflow/include/cflow/machine_runtime.h`
- Create: `cflow/src/machine_runtime.c`
- Create: `cflow/tests/cflow_machine_runtime_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: immutable `cflow_machine` queries, `cflow_mailbox_init`, CMeta trait/type equality APIs, and `cflow_executor_has`.
- Produces: `cflow_machine_instance`, binding/config/status/stat types, init/query/destroy functions, and installed aggregate-header visibility.

- [ ] **Step 1: Add the failing initialization and header tests**

  Add `cflow_machine_runtime_test.c` with literal two-state Machine fixtures. Assert that valid exact bindings initialize, missing/duplicate/unknown/NULL bindings fail with `CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH`, ManualExecutor is rejected, non-trivial or heterogeneous VALUE types are rejected, failed init leaves `instance.impl == NULL`, and initial state bytes are copied. Extend the C++ header test to zero-initialize the new public types. The production breaks caught are partial publication, callback-domain drift, implicit manual execution, and unsafe byte ownership.

- [ ] **Step 2: Run the focused target and verify RED**

  Run:

  ```powershell
  cmake --build --preset win-release-user --target cflow_machine_runtime_test
  ```

  Expected: build fails because `cflow/machine_runtime.h` and the target do not exist.

- [ ] **Step 3: Implement the minimum transactional runtime shell**

  Define the approved callbacks and config:

  ```c
  typedef bool (*cflow_machine_guard_fn)(
      void *, const void *, const void *, bool *, const char **);
  typedef bool (*cflow_machine_action_fn)(
      void *, const void *, const void *, void *, void *, const char **);

  typedef struct cflow_machine_instance_config {
      const cflow_machine *machine;
      const void *initial_state;
      const cmeta_type_desc *output_type;
      const cflow_machine_guard_binding *guards;
      size_t guard_count;
      const cflow_machine_action_binding *actions;
      size_t action_count;
      size_t mailbox_capacity;
      cflow_executor *executor;
  } cflow_machine_instance_config;
  ```

  Copy/sort binding rows, validate exact declaration coverage, validate the supported trivial type fragment, allocate exact scratch/state/output buffers with checked arithmetic, initialize Mailbox from copied Machine schema rows, and publish only after every step succeeds. Add read-only current-state/stats/error queries and idempotent quiescent destroy.

- [ ] **Step 4: Reconfigure, build, and verify GREEN**

  Run the Release configure preset because the new source/test are CMake inputs, then build and execute `cflow_machine_runtime_test`. Expected: the initialization group passes with no compiler warnings.

- [ ] **Step 5: Commit the admission slice**

  ```text
  feat(cflow): add machine runtime admission contract
  ```

### Task 2: Serial transition evaluator and Resumable demand mapping

**Files:**
- Modify: `cflow/src/machine_runtime.c`
- Modify: `cflow/tests/cflow_machine_runtime_test.c`

**Interfaces:**
- Consumes: normalized bindings/config from Task 1, Machine canonical rows, Mailbox receive/waitable, executor `try_post`, and `cflow_resumable`.
- Produces: `cflow_machine_instance_try_send`, `cflow_machine_instance_as_resumable`, executor-only transition commits, and VALUE/WAIT/VALUE_AND_DONE/DONE/ERROR steps.

- [ ] **Step 1: Add failing literal trace and demand tests**

  Add one independent test reference evaluator and literal expected traces for priority guard selection, action NONE, action VALUE, self-emitted EVENT, DONE target, ERROR target, no enabled transition, and action failure. Drive the real adapter through SerialExecutor and assert no guard/action executes before `resume`, one demanded VALUE may consume multiple Events, and runtime/referee traces agree. Each expectation is a literal state/value/Event/error sequence rather than a value computed with production helpers.

- [ ] **Step 2: Run the focused test and verify RED**

  Expected failure: `try_send`/`as_resumable` are absent or the shell never progresses from WAIT.

- [ ] **Step 3: Implement the executor-owned evaluator**

  Binary-search canonical Machine rows by ID and `(source,event)` range. Reserve one executor task without holding the instance mutex, reject FULL/CLOSED admission as the first terminal error, and process at most `CFLOW_MACHINE_RUNTIME_QUANTUM` Events per task. Guard/action callbacks run unlocked on the SerialExecutor. EVENT output is copied into the same Mailbox before state commit; VALUE output is copied into one prepared slot. Commit target state once only after every callback/output check succeeds.

- [ ] **Step 4: Implement Resumable WAIT/wake and output delivery**

  Add a single-consumer waitable with arm/cancel, immediate wake after readiness, and in-flight wake accounting. `resume` consumes one prepared VALUE, otherwise schedules serial work and returns WAIT; it never executes a transition inline. Map a terminal value to VALUE_AND_DONE and preserve the first owned error string.

- [ ] **Step 5: Run focused and adjacent tests**

  Run `cflow_machine_runtime_test`, `cflow_machine_test`, `cflow_event_mailbox_test`, and `cflow_execution_test`. Expected: all pass and every runtime callback probe reports maximum concurrency one.

- [ ] **Step 6: Commit the evaluator slice**

  ```text
  feat(cflow): execute machines through resumable serial runtime
  ```

### Task 3: Source integration and lifecycle closure

**Files:**
- Modify: `cflow/src/machine_runtime.c`
- Modify: `cflow/tests/cflow_machine_runtime_test.c`
- Modify: `cflow/tests/cflow_runtime_test.c`

**Interfaces:**
- Consumes: Task 2 Resumable, existing Source interface, normalized Graph/Run, scheduler and sink callbacks.
- Produces: `cflow_machine_instance_as_source`, close/cancel, terminal polling/waker binding, complete Event terminal accounting, and Run integration.

- [ ] **Step 1: Add failing lifecycle and Run integration tests**

  Cover WAIT/wake through `cflow_run`, downstream demand, sink rejection, close inside action callback, repeated/reentrant close, cancel inside callback with source-state preservation, queued-event cancellation, close with a prepared VALUE, concurrent producer admission, and repeated init/attach/run/close/destroy cycles. At terminal quiescence assert literal accounting:

  ```c
  check_equal(stats.accepted,
              stats.completed + stats.failed + stats.cancelled);
  check_equal(stats.pending, (size_t)0u);
  check_equal(stats.in_flight, (size_t)0u);
  ```

- [ ] **Step 2: Run the focused test and verify RED**

  Expected failure: Source attachment and close/cancel semantics are absent.

- [ ] **Step 3: Implement Source and terminal wake integration**

  Source `resume` delegates to the Machine Resumable, cancel requests instance cancellation, destroy detaches the adapter, terminal binding stores/clears the Run waker, and polling exposes OPEN/DONE/ERROR. Allow one attached Resumable or Source at a time and keep the caller-owned instance alive until attachment destroy.

- [ ] **Step 4: Implement close/cancel state machines**

  Close rejects new sends, lets an already executing callback commit, cancels queued Events, and yields DONE or VALUE_AND_DONE. Cancel discards an executing callback result, preserves source state, cancels queued Events, and yields DONE. Clear Mailbox/downstream/terminal waits, synchronize extracted wake callbacks, and make repeated calls idempotent.

- [ ] **Step 5: Run lifecycle, runtime, and sanitizer tests**

  Run focused Release tests, then configure/build `win-dev-user` and run the same test group under ASan. Expected: no failures, sanitizer reports, deadlocks, or stale callbacks.

- [ ] **Step 6: Commit the lifecycle slice**

  ```text
  fix(cflow): close machine runtime lifecycle races
  ```

### Task 4: Lean trace refinement

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/MachineRuntime.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/MachineRuntime.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/MachineRuntime.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: `Machine.SmallStep`, `MachineObservation`, typed Event schema and runtime step-kind projection.
- Produces: supported-fragment predicate, WAIT/transition runtime relation, `runtime_step_trace_refines_machine`, finite sequence refinement, and executable witnesses.

- [ ] **Step 1: Add the failing Lean witnesses**

  Construct literal witnesses for WAIT with no Event, VALUE, VALUE_AND_DONE, no-output active transition, and ERROR. State expected trace suffixes literally. Import the missing runtime module so the focused Lean command fails on absent declarations.

- [ ] **Step 2: Verify Lean RED**

  Run:

  ```text
  lake env lean Test/PhaseATests/MachineRuntime.lean
  ```

  Expected: fail because `CFlow.MachineRuntime` does not exist.

- [ ] **Step 3: Implement runtime projection and proofs**

  Define the admitted homogeneous-value fragment, runtime outcome, trace suffix, and an inductive runtime step whose transition constructor carries a `Machine.SmallStep` witness. Prove WAIT consumes no Event/trace, transition trace suffix equality, and list composition by induction.

- [ ] **Step 4: Run focused Lean and full Lake tests**

  Expected: `MachineRuntime.lean`, aggregate Phase A tests, and `lake test` pass without `sorry` or `admit`.

- [ ] **Step 5: Commit the formal slice**

  ```text
  formal(cflow): prove machine runtime trace refinement
  ```

### Task 5: Documentation, verification, and PR delivery

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-24-cflow-machine-runtime-design.md`
- Modify: `docs/superpowers/plans/2026-08-24-cflow-machine-runtime.md`

**Interfaces:**
- Consumes: completed public API, tests, proof names, and observed command output.
- Produces: ownership/API documentation, repeatable verification record, clean commits, pushed branch, and GitHub PR linked to issue #64.

- [ ] **Step 1: Document the public lifecycle**

  Add one complete C example showing Machine build, SerialExecutor creation,
  instance init, Source attachment, Run demand, close order, and destroy order.
  Document exact callback output buffers, error lifetime, supported types,
  Event accounting, and the prohibition on destroying borrowed dependencies
  early.

- [ ] **Step 2: Run final verification**

  Build `Salts::CFlow` and all affected tests with Release, run the complete
  CFlow CTest set, repeat focused tests with `win-dev-user` ASan, run focused and
  full Lean tests, verify the installed header consumer, and run
  `codegraph affected` for changed implementation/header files. Record any
  platform evidence limited to Windows as a residual CI requirement.

- [ ] **Step 3: Review the diff and plan coverage**

  Check `git diff --check`, inspect every changed file, verify no placeholder
  comments or focused-test markers exist, confirm all issue verification rows
  have tests/proofs, and confirm `.codegraph/` and build products are ignored.

- [ ] **Step 4: Commit delivery documentation**

  ```text
  docs(cflow): document machine resumable runtime
  ```

- [ ] **Step 5: Push and create the PR**

  Push `feat/cflow-machine-runtime`, create a PR against `master`, include
  `Closes #64`, summarize ownership and compatibility, list exact verification
  commands/results, and identify Linux/macOS/sanitizer CI as remote evidence.
