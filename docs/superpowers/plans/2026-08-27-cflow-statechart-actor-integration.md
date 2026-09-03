# CFlow Statechart Runtime Projection and Actor Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose Statechart terminal readiness to CFlow and let the existing opaque Actor lifecycle own and serve a Statechart instance without inventing XML or Source output semantics.

**Architecture:** Add a single-waiter terminal projection directly to the Statechart runtime, then select Machine or Statechart through an internal Actor backend tag. Keep all semantic state in the existing runtime instance and reuse the existing Actor producer-ref control block and lifecycle states.

**Tech Stack:** C11, CMeta interfaces, Salts thread primitives, CFlow Statechart/Mailbox/Executor/Actor, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-27-cflow-statechart-actor-integration-design.md`

## Global Constraints

- Existing Machine Actor and Statechart behavior remains source compatible and unchanged.
- Statechart runtime remains the sole mutable semantic fact source.
- Every queue and storage path keeps its existing positive hard bound; no fallback allocation or retry is added.
- Producer admission remains non-blocking and maps exact Mailbox/lifecycle outcomes.
- Terminal callbacks run outside Statechart and Actor locks.
- No XML parser, SCXML frontend, or fabricated Source output is introduced.
- Every production behavior follows a witnessed RED/GREEN TinyTest cycle.

---

### Task 1: Add the borrowed Statechart terminal projection

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`

**Interfaces:**
- Consumes: existing Statechart terminal winner, `done`, first owned error, instance lock, and `cflow_waitable`.
- Produces: `cflow_statechart_instance_poll_terminal()` and `cflow_statechart_instance_terminal_waitable()` with one-waiter exactly-once notification.

- [x] **Step 1: Write failing terminal projection tests**

  Add tests that poll OPEN, arm one waiter, reject a second concurrent arm,
  cancel and re-arm, wake once on close and clean root-final completion, invoke
  inline after terminal publication, and return ERROR with the instance-owned
  diagnostic after an action/runtime failure.

- [x] **Step 2: Build and witness RED**

  Run:

  ```text
  cmake --build --preset win-release-user --target cflow_statechart_runtime_test
  ```

  Expected: compile failure because the terminal status and projection APIs do
  not exist.

- [x] **Step 3: Implement the minimal terminal projection**

  Add one waiter slot to the opaque instance, implement waitable arm/cancel and
  terminal poll, detach the waiter exactly when `done` becomes published, and
  invoke it only after releasing the instance lock. Preserve mailbox-detach
  wake ordering and invoke both independent wakers when a terminal winner also
  cancels external admission.

- [x] **Step 4: Verify GREEN and Statechart shutdown regressions**

  Run the focused target and `ctest --preset win-release-user -R
  "^cflow_statechart_runtime_test$" --output-on-failure`.

- [x] **Step 5: Commit the terminal projection**

  Commit the header, implementation, and focused test as
  `feat(cflow): expose statechart terminal projection`.

### Task 2: Add the Statechart Actor backend

**Files:**
- Modify: `cflow/include/cflow/actor.h`
- Modify: `cflow/src/actor.c`
- Create: `cflow/tests/cflow_statechart_actor_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 terminal waitable/poll, existing `cflow_actor` owner/ref handles, Actor gate/refcount protocol, Statechart init/send/close/cancel/destroy, and existing Actor state/send enums.
- Produces: `cflow_actor_init_statechart()` and `cflow_actor_get_statechart_stats()`; existing lifecycle/ref/send functions dispatch by private backend kind.

- [x] **Step 1: Write failing Statechart Actor tests**

  Build one literal compound/initial/atomic/final Statechart fixture. Assert
  exact init rejection, pre-start rejection, start, wrong-type, capacity-one
  FULL, accepted transition to final, clean STOPPED callback, stop from START
  and RUNNING, retained-ref STALE after destruction, failure propagation, and
  Statechart accounting snapshots.

- [x] **Step 2: Reconfigure/build and witness RED**

  Reconfigure because a test target is added, then build
  `cflow_statechart_actor_test`. Expected: compile failure because the new
  config/result/stats types and constructor do not exist.

- [x] **Step 3: Implement private backend dispatch**

  Append `CFLOW_ACTOR_STATECHART_REJECTED`, add the Statechart public structs,
  initialize the existing opaque Actor shell with a Statechart instance, arm
  its terminal waiter on start, map Mailbox outcomes in the shared send path,
  and dispatch stop/stats/destroy without changing the Machine branch.

- [x] **Step 4: Verify GREEN and existing Actor/Machine parity**

  Run `cflow_statechart_actor_test`, `cflow_actor_test`,
  `cflow_machine_runtime_test`, and `cflow_statechart_runtime_test` through the
  Release CTest preset.

- [x] **Step 5: Commit the Actor backend**

  Commit public API, implementation, CMake registration, and tests as
  `feat(cflow): add statechart actor facade`.

### Task 3: Publish contracts and verify installed consumption

**Files:**
- Modify: `cflow/include/cflow/cflow.h` only if aggregate inclusion changes are required
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/plans/2026-08-27-cflow-statechart-phase1.md`
- Modify: `docs/superpowers/specs/2026-08-27-cflow-statechart-phase1-design.md` only where the stale completed Source claim appears
- Modify: `docs/superpowers/specs/2026-08-27-cflow-statechart-actor-integration-design.md`
- Modify: `docs/superpowers/plans/2026-08-27-cflow-statechart-actor-integration.md`

**Interfaces:**
- Consumes: verified Task 1/2 API and ownership behavior.
- Produces: installed-header coverage and an exact public lifecycle/data-path contract that explicitly defers Source until typed output semantics exist.

- [x] **Step 1: Add aggregate C++ compile coverage**

  Zero-initialize every new enum-bearing config/result/stats type, construct a
  terminal waitable expression, and compile through `<cflow/cflow.h>`.

- [x] **Step 2: Correct stale documentation and add the public example**

  Document construction, start/send/stop/wait/destroy order, borrowed objects,
  MPMC producer refs, capacities, exact statuses, terminal callback rules, and
  why this phase intentionally has no Statechart Source or XML parser. Change
  the stale Phase 1 plan line from a completed Source/Resumable claim to the
  terminal projection actually delivered here.

- [x] **Step 3: Verify Release, ASan, and installed consumers**

  Build the CFlow target and focused tests, run the full CFlow CTest family,
  run the corresponding `win-dev-user` focused tests, and run the repository's
  isolated `verify_installed_package` install/consumer target.

- [x] **Step 4: Sync CodeGraph and inspect affected tests**

  Run `codegraph sync .` and `codegraph affected -p .` over the modified public
  headers and implementations. Execute any additional affected test target not
  already covered.

- [x] **Step 5: Commit documentation and verification metadata**

  Commit aggregate coverage, docs, corrected plan claims, and checked plan
  state as `docs(cflow): document statechart actor integration`.

## Verification evidence (2026-08-27)

- TDD RED: the new Statechart Actor target failed compilation on the absent
  Statechart public types, status, constructor, and stats query. The terminal
  projection RED independently failed on its absent status and APIs.
- Windows Release: the complete build passed 461/461 steps; all `cflow_*`
  tests passed 34/34. Focused Actor, Statechart, Machine runtime, and aggregate
  C++ header tests were also run during the RED/GREEN cycles.
- Windows ASan (`win-dev-user`): the Statechart Actor, existing Actor, Machine
  runtime, Statechart runtime, and aggregate C++ header tests passed 5/5.
- Installed package: `verify_installed_package` installed into its isolated
  build-tree prefix and configured/built 18/18 C/C++ `find_package` consumer
  steps, including the CFlow consumer.
- Documentation: the exact Statechart Actor README code fence passed Clang C11
  syntax checking against the repository public include paths.
- Structure: `codegraph sync .` reported the index current; `codegraph affected`
  identified seven adjacent CFlow tests, all covered by the 34/34 Release run.
