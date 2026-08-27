# CFlow Format-neutral Statechart Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded, deterministic native Statechart IR/runtime with parallel configurations, history, ordered executable actions, eventless/completion processing, dual event queues, configuration queries, scoped timers, and Lean proofs while preserving existing Machine APIs.

**Architecture:** New opaque `cflow_statechart` and `cflow_statechart_instance` objects sit beside the existing single-leaf Machine. Build normalizes a tree and ordered declaration rows; one borrowed SerialExecutor owns all runtime mutation. Each microstep uses preallocated staged state/configuration/history buffers and publishes once after exit, transition, and entry phases succeed.

**Tech Stack:** C11, CMeta type/effect contracts, CFlow Event/Mailbox/Executor/Clock/Timer Event, Turbo Platform synchronization, TinyTest, CMake Presets, Lean 4.33.1 and Lake.

**Spec:** `docs/superpowers/specs/2026-08-27-cflow-statechart-phase1-design.md`

## Global Constraints

- [x] Existing Machine, hierarchy, Actor, Graph, Run, Source, executor, Mailbox, and timer public behavior remains unchanged.
- [x] Statechart semantic state has one mutable owner: work executing on the borrowed non-manual SerialExecutor.
- [x] Build/init and each microstep are transactional; failure never publishes partial configuration, history, or extended state.
- [x] External/internal queues, configuration, history, selected-transition scratch, timers, and per-macrostep work have explicit hard bounds.
- [x] No XML, data-model interpreter, external communication, durable workflow, inline execution fallback, implicit worker, or unbounded allocation is introduced.
- [x] New C behavior follows a witnessed RED/GREEN TinyTest cycle; new Lean behavior follows a witnessed failing/passing focused file.
- [x] Public objects remain opaque and all borrowed/owned lifetimes, errors, shutdown rules, and snapshot invalidation are documented.

## Task 1: Define and validate the immutable native Statechart IR

**Files:**
- Create: `cflow/include/cflow/statechart.h`
- Create: `cflow/src/statechart.c`
- Create: `cflow/tests/cflow_statechart_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: `cflow_event_type`, CMeta type/effect/property descriptors, and configured Machine ceilings.
- Produces: state/trigger/transition/executable/action-row declarations, build status, opaque handle, and immutable row queries.

- [x] **Step 1: Add public compile-shape and build-rejection tests**

  Add literal fixtures for one compound root, one parallel root with two
  compound regions, initial pseudo-nodes, final nodes, and shallow/deep history.
  Assert exact statuses for duplicate IDs/order, cycles, multiple roots,
  missing/multiple initial defaults, illegal pseudo-state transitions, unknown
  completion state, unknown action/guard/Event, ambiguous priority, type
  mismatch, zero/over-limit counts, and failed-build empty output. Extend the
  C++ aggregate-header test with zero-initialized public types.

- [x] **Step 2: Build and witness RED**

  Reconfigure because source/test globs change, then build
  `cflow_statechart_test`. Expected failure: the header, target, and symbols do
  not exist.

- [x] **Step 3: Implement copied normalized tree storage**

  Define opaque implementation tables for sorted IDs, dense indices,
  parent/depth, child spans, document order, transition spans, default
  transitions, state-action spans, and transition-action spans. Use checked
  addition/multiplication before every allocation. Validate all tree and typed
  declaration invariants before assigning `out->impl`. State document order is
  a hierarchy-compatible depth-first preorder: every parent precedes its
  descendants and every subtree occupies one contiguous interval.

- [x] **Step 4: Implement immutable queries and aggregate exposure**

  Add count/row queries returning borrowed normalized rows valid until destroy.
  Include the installed header from `cflow/cflow.h`, add the source to
  `TurboUtils::CFlow`, and compile both C11 and C++17 consumers.

- [x] **Step 5: Verify GREEN and adjacent regressions**

  Run `cflow_statechart_test`, `cflow_machine_test`,
  `cflow_machine_hierarchy_test`, and `cflow_header_cpp_test`.

## Task 2: Implement legal active configurations and initial/default entry

**Files:**
- Create: `cflow/include/cflow/statechart_runtime.h`
- Create: `cflow/src/statechart_runtime.c`
- Create: `cflow/src/statechart_internal.h`
- Create: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: immutable normalized Statechart, CMeta trivial state traits, borrowed SerialExecutor, and exact guard/executable bindings.
- Produces: transactional instance init, active-configuration snapshot, exclusive-fragment current-state projection, copied extended-state query, stats, first error, and destroy.

- [x] **Step 1: Add failing initial-configuration and snapshot tests**

  Assert literal document-ordered configurations for nested compound entry and
  two parallel regions. Assert pseudo-nodes are absent, every ancestor is
  present, parallel children are all active, and exclusive `current_state`
  returns one leaf while parallel returns zero. Assert insufficient snapshot
  capacity reports the exact required count and leaves the output untouched.

- [x] **Step 2: Build and witness RED**

  Expected failure: Statechart instance and configuration query APIs are absent.

- [x] **Step 3: Implement bounded double-buffer configuration storage**

  At init allocate two bitsets, two state lists, two history tables, two
  extended-state buffers, and fixed work arrays sized from validated IR counts.
  Iteratively over preallocated index stacks compute compound initial and
  parallel default entry into the staged buffer, validate legal-configuration
  invariants, then publish once. Do not recurse or allocate in entry
  computation.

- [x] **Step 4: Implement transactional queries and lifecycle shell**

  Copy configuration/version and extended state under the instance mutex.
  Normalize exact executable bindings but do not execute them yet. Init failure
  clears all allocations; destroy requires executor/caller quiescence.

- [x] **Step 5: Verify GREEN**

  Run the focused runtime test and mutation-check wrong-child, missing-ancestor,
  duplicate-state, partial-copy, and parallel-current-state branches.

## Task 3: Implement deterministic transition selection and conflicts

**Files:**
- Modify: `cflow/src/statechart_internal.h`
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Statechart.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Statechart.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/Statechart.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: active leaf list, normalized ancestry/transition spans, exact guard bindings, and trigger value.
- Produces: ordered immutable selected-transition IDs and exit-set bitsets.

- [x] **Step 1: Add failing C selection traces**

  Use two parallel regions to assert: one compatible transition per region;
  child transition preempts ancestor transition; two exit-set-conflicting
  transitions choose the earlier leaf unless one source is a descendant;
  targetless transitions coexist; guards observe the same published state;
  and repeated runs yield the same literal selected-ID sequence.

- [x] **Step 2: Add failing Lean selection witnesses**

  Define literal candidates/exit sets and expected conflict-filter results.
  Import the absent Statechart module and run the focused Lean file to witness
  failure.

- [x] **Step 3: Implement candidate enumeration and filtering**

  Visit active leaves in document order, walk each ancestry chain, evaluate
  first enabled rows in priority/order, deduplicate transition IDs, compute
  preallocated exit bitsets from transition domains, and apply the specified
  intersection/preemption algorithm without state mutation.

- [x] **Step 4: Prove deterministic conflict-free selection**

  Model ordered candidates and exit sets. Prove the filter result is
  conflict-free, descendant preemption is respected, and evaluation is a
  function yielding one deterministic list.

- [x] **Step 5: Verify C and Lean GREEN**

  Run the focused TinyTest executable, focused Lean file, and aggregate Phase A
  Lean tests.

## Task 4: Implement ordered microsteps, executable actions, and atomic commit

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Statechart.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Statechart.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/Statechart.lean`

**Interfaces:**
- Consumes: selected transition set, staged configuration/history/state, ordered executable bindings, and optional trigger Event.
- Produces: one committed configuration/version/state or one first terminal error with no semantic publication.

- [x] **Step 1: Add failing exact action-order tests**

  Capture literal action traces for cross-region transition sets: all exits in
  descendant-first/reverse-document order, then transition actions in selection
  order, then entries in ancestor-first/document order. Cover internal versus
  external descendant transitions and targetless actions. Assert sequential
  staged state updates.

- [x] **Step 2: Add failing atomicity/error tests**

  Fail one exit, transition, and entry action separately. Assert the published
  configuration/version/state remain unchanged, later actions do not run, the
  first error is stable, and accepted work settles failed once.

- [x] **Step 3: Implement exit/history/action/entry staging**

  Copy published buffers to staged buffers; save shallow/deep history before
  exit actions; compute union exit/entry sets; invoke ordered actions unlocked
  but only from the serial executor; validate callback contract; and atomically
  stage callback-raised internal Events outside the published FIFO, and
  atomically swap staged/published buffers plus append staged Events after all
  phases succeed.

- [x] **Step 4: Extend Lean legality and ordering proofs**

  Prove exit/entry order predicates, legal-configuration preservation for the
  modeled microstep constructor, and shallow/deep history restoration
  predicates. The C callback and allocation boundary remains explicit.

- [x] **Step 5: Verify GREEN and existing runtime regressions**

  Run Statechart runtime, Machine runtime, hierarchy, Actor, and executor tests.

## Task 5: Implement bounded internal/external queues and run-to-completion

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Statechart.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Statechart.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/Statechart.lean`

**Interfaces:**
- Consumes: existing bounded typed Mailbox for external MPSC admission, one instance-owned internal FIFO, SerialExecutor scheduling, eventless triggers, and completion triggers.
- Produces: `try_send`, bounded internal raise, macrostep scheduling, close/cancel, Source/Resumable terminal projection, and complete accounting.

- [x] **Step 1: Add failing run-to-completion tests**

  Assert initialization stabilizes eventless transitions; one external Event
  runs all following eventless/internal/completion microsteps before the next
  accepted external Event; completion fires once for compound/parallel states;
  a final root terminates; unhandled Events are discarded rather than treated
  as runtime errors; and macrostep traces are deterministic.

- [x] **Step 2: Add failing capacity/cycle/lifecycle tests**

  Cover external FULL/CLOSED/type mismatch, internal FULL, completion overflow,
  zero/invalid capacities, eventless cycle exceeding `microstep_limit`, executor
  FULL/CLOSED, close during action, cancel before/after commit, concurrent
  producers, and terminal accounting identity.

- [x] **Step 3: Implement dual-queue scheduling**

  Reuse `cflow_mailbox` for external copied Events. Implement a fixed internal
  FIFO over preallocated typed slots plus completion-trigger rows. After one
  external dequeue, repeatedly prefer eventless selection, then internal FIFO,
  until quiescent. Schedule every quantum through the non-manual SerialExecutor;
  never execute inline.

- [x] **Step 4: Implement Source/Resumable and shutdown protocol**

  Expose WAIT/DONE/ERROR and optional action VALUE observations through the
  existing adapter pattern. Stop admission first, linearize commit versus
  cancel under the instance mutex, cancel queued Events/timers, detach waits,
  and wait for executor idle before storage destruction.

- [x] **Step 5: Extend Lean macrostep trace refinement**

  Model eventless-before-internal-before-external ordering and finite bounded
  macrosteps. Prove trace concatenation and add a C conformance-row projection
  theorem analogous to existing Machine runtime refinement.

- [x] **Step 6: Verify GREEN**

  Run Statechart, Mailbox, executor, runtime, Source/Run, and aggregate Lean
  tests.

## Task 6: Extend scoped timers to active configurations

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/src/timer_event_internal.h`
- Modify: `cflow/src/timer_event.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `cflow/tests/cflow_timer_event_test.c`

**Interfaces:**
- Consumes: published configuration bitset, Clock, fixed Timer Event queue, committed union exit set, and external Event admission.
- Produces: scoped schedule/cancel/fire APIs and per-commit batch scope cancellation.

- [x] **Step 1: Add failing configuration-scope tests**

  Schedule timers in both parallel regions and their ancestors. Exit one region
  and assert only its exited-state timers cancel; exit the parallel parent and
  assert every descendant scope cancels. Cover inactive scope rejection,
  equal-deadline FIFO, FIRE_WON, external-queue FULL, close, and cancel.

- [x] **Step 2: Implement bitset scope admission and batch cancellation**

  Check scope membership under the Statechart publication gate. On successful
  microstep, pass the exact exited ID set to one Timer queue batch-cancel call
  before configuration publication. Fired timer Events enter the external
  Mailbox and do not bypass the current macrostep.

- [x] **Step 3: Verify GREEN and hierarchy timer compatibility**

  Run Statechart runtime, Timer Event, hierarchy, and temporal tests.

## Task 7: Add exclusive-hierarchy compatibility evidence and documentation

**Files:**
- Create: `cflow/include/cflow/statechart_hierarchy_adapter.h`
- Create: `cflow/src/statechart_hierarchy_adapter.c`
- Create: `cflow/tests/cflow_statechart_hierarchy_adapter_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/README.md`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Statechart.lean`

**Interfaces:**
- Consumes: existing immutable hierarchy declaration and existing action/guard semantics in the admitted exclusive subset.
- Produces: explicit build adapter and differential traces; it does not redirect the old runtime.

- [x] **Step 1: Add failing differential tests**

  Compile literal existing hierarchy fixtures into Statechart declarations and
  assert equal initial leaf, selected transition, LCA exit/entry route, final
  result, first error, and scoped timer cancellation. Reject declarations that
  cannot preserve callback/observation contracts exactly.

- [x] **Step 2: Implement a thin explicit adapter**

  Map one initial-child chain to initial pseudo-nodes and existing transitions
  to external Event transitions. Keep existing hierarchy and Machine handles
  caller-owned and do not cache a second mutable state.

- [x] **Step 3: Prove exclusive projection**

  Show that legal Statechart configurations without parallel/history contain
  one active leaf and that candidate/route projection matches the existing
  hierarchy model for the admitted fixture domain.

- [x] **Step 4: Document the supported native fragment**

  Add one complete C example, declaration rules, queue/limit guidance,
  configuration queries, callback ownership, timer scope, shutdown order,
  explicit proof boundary, and the statement that this is not an SCXML
  processor or durable workflow engine.

## Task 8: Final verification, review, and PR delivery

**Files:**
- Modify: `docs/superpowers/plans/2026-08-27-cflow-statechart-phase1.md`
- Modify: `docs/superpowers/specs/2026-08-27-cflow-statechart-phase1-design.md`

- [x] **Step 1: Run Windows focused and full verification**

  Reconfigure/build `win-release-user`; run all Statechart tests, all CFlow
  tests, and then full CTest. Configure/build `win-dev-user` and repeat focused
  Statechart/Machine/Timer tests under ASan. Run C++/installed header consumers.

- [x] **Step 2: Run complete Lean verification**

  Run the focused Statechart Lean file, aggregate Phase A tests, and `lake test`.
  Search with `rg.exe` for `sorry`, `admit`, focused TinyTest markers, and
  unowned placeholder comments in changed files.

- [x] **Step 3: Run remote Linux verification**

  Push the branch, use `root@eu` to fetch it into an isolated remote worktree,
  configure/build/test with `linux-release-user`, and run the focused Linux
  sanitizer profile available on that host. Record exact commands/results.

- [x] **Step 4: Review impact and diff**

  Run `codegraph sync .`, `codegraph affected` for public headers and runtime
  sources, `git diff --check`, inspect every changed file, verify `.codegraph`
  and build products are ignored, and confirm every Phase 1 issue row maps to a
  C test and Lean theorem or explicit proof boundary.

- [x] **Step 5: Commit, push, and create PR**

  Use scoped commits as tasks become green. Push
  `feat/cflow-statechart-phase1`, create a PR against `master` with `Refs #122`
  until every issue phase remains tracked, include exact local/remote/CI
  evidence, and do not merge until required CI passes.

## Verification evidence (2026-08-27)

- Windows Release: `cmake --preset win-release-user` with the local pkgconf and
  SIMDE paths, followed by `cmake --build --preset win-release-user`, completed
  all 513 build steps. Statechart tests passed 3/3, all CFlow tests passed
  31/31, and full CTest passed 149/149. The incremental
  `verify_installed_package` target also built and linked all installed C/C++
  consumers.
- Windows ASan: `cmake --build --preset win-dev-user --target` for the eight
  Statechart/Machine/Timer/Temporal/C++ targets followed by direct execution in
  the Visual Studio developer environment passed 162 tests and 3423 assertions
  with zero failures.
- Lean: `lake env lean CMetaCFlowCalculus/Proofs/Statechart.lean`,
  `lake env lean Test/PhaseATests.lean`, and `lake test` all passed; the full
  build completed 81/81 targets. The changed Statechart C/Lean files contain no
  `sorry`, `admit`, `TODO`, `FIXME`, `HACK`, placeholder abort, or focused test.
- Linux Release on `root@eu`: in isolated worktree
  `/root/codex-worktrees/turbo-utils-statechart-c8b4f2a`, run
  `PROJECT_ROOT="$PWD" VCPKG_ROOT=/opt/vcpkg cmake --preset linux-release-user
  -DENABLE_TESTS=ON`, then `cmake --build --preset linux-release-user -j2`.
  All 534 build steps passed; focused Statechart tests passed 8/8, all CFlow
  tests passed 31/31, and full CTest passed 154/154.
- Linux ASan on `root@eu`: configure with
  `PROJECT_ROOT="$PWD" VCPKG_ROOT=/opt/vcpkg cmake --preset linux-dev-user
  -DENABLE_TESTS=ON`, build the same eight focused targets, then run focused
  CTest. All 8/8 tests passed under AddressSanitizer.
- Final structural review: `codegraph sync .` succeeded; `codegraph affected`
  identified the three expected Statechart test files; `git diff --check`
  passed. Build trees and `.codegraph/` remained absent from the Git diff.
- Delivery: scoped commits were pushed to `feat/cflow-statechart-phase1` and
  [PR #124](https://github.com/qigao/turbo-utils/pull/124) was opened against
  `master` with `Refs #122`. Merge remains gated on required CI.
