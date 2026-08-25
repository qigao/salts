# CFlow Managed Stream Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task with review checkpoints.

**Goal:** Make interpreted CFlow/TurboSTL `Stream<T>` lifecycle-aware for CMeta managed values while preserving trivial fast paths and byte-storage admission boundaries.

**Architecture:** Keep normalized Graph as the immutable type/topology fact source and replace every interpreted retained byte buffer with the existing `cflow_value_slot` ownership state machine. Generated container ranges construct managed values; Run, SubRun, Coord, and Relation transfer those values through explicit copy/move/destroy transitions.

**Tech Stack:** C11, CMeta type traits/ranges/callables, CFlow Graph/Run/Resumable/Relation, TurboSTL typed containers, TinyTest, CMake presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-managed-stream-lifecycle-design.md`

## Global Constraints

- Preserve all public function and struct ABIs.
- Keep compiled plans, direct byte results, and `to_array()` trivial-only.
- A callback failure leaves destination storage empty; no implicit fallback.
- Every live slot is moved once or destroyed once on DONE/ERROR/CANCEL/CLOSE.
- Use existing CMeta traits and TurboSTL containers; add no dependency.

---

### Task 1: Lock the lifecycle contract with failing tests

**Files:**
- Modify: `cflow/tests/cflow_runtime_test.c`
- Modify: `turbostl/tests/turbostl_entry_test.c`

- [x] Add a managed generated-container Range test that requires
  `CMETA_RANGE_CONSTRUCTS_VALUES` and an independent copy.
- [x] Replace managed-operator rejection coverage with filter/map/reduce Run
  ownership assertions, including cancellation/error cleanup.
- [x] Add managed value/coordination/SubRun coverage where retained state is
  replaced or destroyed.
- [x] Build and run the two focused tests and record the expected red failures.

### Task 2: Centralize value construction and slot transitions

**Files:**
- Modify: `cflow/src/value_storage.h`
- Modify: `cflow/include/cflow/runtime.h`

- [x] Add checked raw-storage construct/destroy helpers used by slot and
  resumable boundaries.
- [x] Remove the source-only operator admission restriction while retaining
  complete lifecycle-trait validation.
- [x] Document that resumables construct output only on value steps.
- [x] Run focused CFlow tests.

### Task 3: Make generated ranges construct managed elements

**Files:**
- Modify: `cmeta/include/cmeta/container.h`
- Modify: `turbostl/include/turbostl/detail/typed_facade.h` if required
- Test: `turbostl/tests/turbostl_entry_test.c`

- [x] Route index/link/slot/key/value/entry range output through the element
  descriptor's copy constructor.
- [x] Compute `CMETA_RANGE_CONSTRUCTS_VALUES` from the actual element type.
- [x] Verify failed construction does not advance ownership or leak.
- [x] Run focused TurboSTL range tests.

### Task 4: Convert the linear Run operator path

**Files:**
- Modify: `cflow/src/runtime.c`
- Test: `cflow/tests/cflow_runtime_test.c`

- [x] Convert path, map/transform output, reduce accumulator, source transfer,
  and sink cleanup to `cflow_value_slot`.
- [x] Convert flatMap/relation continuation root/output storage to slots.
- [x] Ensure cancel, error, source completion, and deferred close clear all slots.
- [x] Run focused filter/map/reduce/flatMap tests.

### Task 5: Convert Resumable, SubRun, Coord, and Relation retention

**Files:**
- Modify: `cflow/src/coord.c`
- Modify: `cflow/src/subrun.c`
- Modify: `cflow/src/relation_exec.c`
- Modify: `cflow/include/cflow/coord.h`
- Modify: `cflow/include/cflow/subrun.h`
- Test: `cflow/tests/cflow_runtime_test.c`

- [x] Give value machines, SubRuns, coordinator children, and relation last
  results explicit slots.
- [x] Destroy a prior latest value before accepting a newly constructed child
  value; keep borrowed `cflow_coord_value()` semantics explicit.
- [x] Materialize fold/invoke/pass-through results transactionally.
- [x] Run coordination, SubRun, relation, cancellation, and error tests.

### Task 6: Regression verification and delivery

**Files:**
- Modify: user-facing CFlow/TurboSTL documentation only where lifecycle boundary
  statements are now stale.

- [x] Configure/build with `win-release-user`; run focused tests first.
- [x] Run all CFlow and TurboSTL tests, then the relevant preset suite.
- [x] Inspect `git diff --check`, public ABI changes, ownership transitions, and
  accidental `.codegraph` inclusion.
- [ ] Commit the coherent implementation, push `feat/cflow-managed-stream`, and
  create a PR with verification evidence and compatibility notes.
