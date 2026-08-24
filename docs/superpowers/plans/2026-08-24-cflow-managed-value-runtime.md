# CFlow Managed Value Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute source-only CFlow graphs containing lifecycle-managed values and deliver them safely to Sink or Collector.

**Architecture:** CMeta Range and CFlow Source capability bits identify callbacks that construct values in empty output storage. An internal aligned `cflow_value_slot` owns each live runtime value and dispatches copy/move/destroy traits. Existing typed operators, Plans, byte results, and non-capable sources remain fail-fast for managed types.

**Tech Stack:** ISO C11, CMeta traits and interfaces, CFlow Run, TinyTest, CMake Presets, MSVC/Ninja.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-managed-value-runtime-design.md`

## Global Constraints

- Preserve `CFlow -> CMeta`; do not introduce a CFlow dependency on TurboSTL or CBind.
- Preserve legacy trivial Source, Range, Plan, and result behavior.
- Do not admit managed values to the existing by-value typed callable ABI.
- Allocation must honor `cmeta_type_desc.align` and reject overflow.
- Every successful construction has exactly one destroy on success, error, cancel, and close.
- Implement each behavior through a RED/GREEN TinyTest cycle.

---

### Task 1: Internal lifecycle slot

**Files:**
- Modify: `cflow/src/value_storage.h`
- Test: `cflow/tests/cflow_runtime_test.c`

**Interfaces:**
- Produces: `cflow_value_slot_init`, `cflow_value_slot_copy`, `cflow_value_slot_move`, `cflow_value_slot_destroy`.
- Consumes: `cmeta_type_desc` COPY/MOVE/DESTROY and trivial trait flags.

- [x] **Step 1: Write tests for owning copy/destroy, copy failure, move, and over-alignment.**
- [x] **Step 2: Build and run `cflow_runtime_test`; verify compilation or assertions fail because slot APIs do not exist.**
- [x] **Step 3: Implement checked aligned allocation and the EMPTY/LIVE slot state machine.**
- [x] **Step 4: Rebuild and verify all new slot tests pass.**

### Task 2: Constructing Source and Range capabilities

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cflow/include/cflow/runtime.h`
- Modify: `cflow/src/sources.c`
- Test: `cflow/tests/cflow_runtime_test.c`

**Interfaces:**
- Produces: `CMETA_RANGE_CONSTRUCTS_VALUES` and `CFLOW_SOURCE_CAP_CONSTRUCTS_VALUES`.
- Consumes: Task 1 raw construction helpers.

- [x] **Step 1: Write tests proving managed arrays and constructing Ranges create Sources while legacy managed Ranges remain rejected.**
- [x] **Step 2: Run the focused test and verify the constructors fail before implementation.**
- [x] **Step 3: Make array resume copy-construct values, accept managed arrays, and gate managed Range admission on its construction flag.**
- [x] **Step 4: Rebuild and verify constructor tests pass while readiness/channel rejection remains green.**

### Task 3: Source-only managed Run

**Files:**
- Modify: `cflow/src/value_storage.h`
- Modify: `cflow/src/runtime.c`
- Test: `cflow/tests/cflow_runtime_test.c`

**Interfaces:**
- Produces: lifecycle-aware source scratch and terminal delivery in `cflow_run_open_subgraph`.
- Consumes: Task 1 slots and Task 2 source capability.

- [x] **Step 1: Write a managed Source/Graph integration test with copy/destroy counters and a real Sink.**
- [x] **Step 2: Run it and verify open fails at the current trivial graph gate.**
- [x] **Step 3: Admit source-only managed graphs with constructing sources, process values through owned slots, and destroy them after callback.**
- [x] **Step 4: Add RED tests for sink rejection and close cleanup, then implement the minimum cleanup paths and verify GREEN.**

### Task 4: Managed Range to Collector transaction

**Files:**
- Modify: `cflow/src/adapters.c`
- Test: `cflow/tests/cflow_runtime_test.c`

**Interfaces:**
- Produces: managed `cflow_eval_collect` support for constructing Ranges.
- Consumes: Task 2 Range source and Task 3 managed Run.

- [x] **Step 1: Write a custom transactional collector test that independently copies managed values.**
- [x] **Step 2: Run it and verify evaluation fails before managed Run support is connected.**
- [x] **Step 3: Keep byte-result adapters trivial-only and route Collector evaluation through the managed Run path.**
- [x] **Step 4: Verify successful commit and abort-on-copy-failure both balance object lifetimes.**

### Task 5: Admission, documentation, and regression

**Files:**
- Modify: `cflow/include/cflow/runtime.h`
- Modify: `cflow/include/cflow/sources.h`
- Modify: `cflow/include/cflow/adapters.h`
- Modify: `cflow/README.md`
- Test: `cflow/tests/cflow_runtime_test.c`
- Test: `cflow/tests/cflow_pipeline_test.c`

**Interfaces:**
- Produces: explicit managed-runtime support matrix and fail-fast diagnostics.
- Consumes: all earlier tasks.

- [x] **Step 1: Add a test proving managed operator graphs and managed legacy Sources remain rejected without ownership transfer.**
- [x] **Step 2: Document source construction, callback borrowing, and remaining trivial-only paths.**
- [x] **Step 3: Run focused CMeta/CFlow/TurboSTL tests, then full Release configure, build, CTest, and install.**
- [x] **Step 4: Run `git diff --check`, placeholder scan, and CodeGraph sync.**
