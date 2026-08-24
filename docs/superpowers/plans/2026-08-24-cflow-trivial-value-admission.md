# CFlow Trivial Value Admission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every current CFlow byte-storage execution path reject non-trivial value types before copying or taking ownership.

**Architecture:** Keep Graph/Stream construction type-generic and centralize execution admission in an internal header. Source constructors protect manual Source use; runtime and Plan gates protect custom sources and graph-produced values. Documentation distinguishes CMeta semantic data from physical contiguous storage.

**Tech Stack:** ISO C11, CMeta traits, CFlow runtime and Plan, TinyTest, CMake Presets, MSVC/Ninja.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-trivial-value-admission-design.md`

## Global Constraints

- Preserve `CFlow -> CMeta`; do not add a CBind, TurboSTL, or Core dependency.
- Require both `CMETA_TRAIT_TRIVIAL_COPY` and `CMETA_TRAIT_TRIVIAL_DESTROY` for current byte-storage execution.
- Do not change Graph/Stream construction semantics or any public structure layout.
- Do not add a contiguous fast path or reinterpret `cmeta_container_data()` as storage.
- Every failure is fail-fast and transactional; no fallback and no Source ownership transfer on failed open.
- Implement with test-first RED/GREEN cycles and verify through `win-release-user` presets under `VsDevCmd.bat`.

---

### Task 1: Source constructor admission

**Files:**
- Create: `cflow/src/value_storage.h`
- Modify: `cflow/src/sources.c`
- Modify: `cflow/tests/cflow_test_ops.h`
- Modify: `cflow/tests/cflow_test_ops.c`
- Test: `cflow/tests/cflow_runtime_test.c`

**Interfaces:**
- Produces: `cflow_value_storage_type_supported(const cmeta_type_desc *)`, an internal static-inline predicate.
- Consumes: CMeta `cmeta_type_require_traits()` and the two trivial-storage flags.

- [x] **Step 1: Add an owning test descriptor and constructor rejection tests**

Define the shared `cflow_test_owned_value` descriptor in `cflow_test_ops.h/.c`.
Its traits include copy, move, and destroy but omit both trivial flags. Add
separate TinyTest cases proving array, Range, channel, and readiness
constructors return false and leave their outputs caller-cleanable/zero.

Keep the existing three-byte overflow test specific by giving its descriptor a test-local trait table containing both trivial flags.

- [x] **Step 2: Run the runtime test and verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target cflow_runtime_test
ctest --preset win-release-user -R '^cflow_runtime_test$' --output-on-failure
```

Expected: the new constructor cases fail because the current constructors admit the owning type.

- [x] **Step 3: Add the internal predicate and source checks**

Implement:

```c
static inline bool
cflow_value_storage_type_supported(const cmeta_type_desc *type) {
    const cmeta_trait_flags required =
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
    return cmeta_type_require_traits(type, required) == CMETA_OK;
}
```

Call it before allocation in `cflow_source_from_array()`,
`cflow_source_from_range()`, `cflow_channel_init()`, and
`cflow_source_from_readiness()`.

- [x] **Step 4: Run the runtime test and verify GREEN**

Run the Task 1 commands again. Expected: `cflow_runtime_test` passes.

### Task 2: Runtime and Plan graph admission

**Files:**
- Modify: `cflow/src/value_storage.h`
- Modify: `cflow/src/runtime.c`
- Modify: `cflow/src/plan_compile.c`
- Consume: `cflow/tests/cflow_test_ops.h`
- Test: `cflow/tests/cflow_runtime_test.c`
- Test: `cflow/tests/cflow_pipeline_test.c`

**Interfaces:**
- Produces: `cflow_graph_value_storage_supported(const cflow_graph *)`, checking every subgraph and node input/output descriptor.
- Consumes: Task 1's type predicate.

- [x] **Step 1: Add runtime and Plan rejection tests**

In the runtime test, implement a real user-defined `cflow_source` whose output
descriptor is the owning type. Prove `cflow_run_open()` returns false and leaves
the Source owned by the caller.

In the pipeline test, normalize a source-only Graph of the owning type. Prove
`cflow_plan_graph_supported()` and `cflow_plan_compile()` both return false,
`plan.impl` remains NULL, and `plan.error` is set.

- [x] **Step 2: Run focused tests and verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target cflow_runtime_test cflow_pipeline_test
ctest --preset win-release-user -R '^cflow_(runtime|pipeline)_test$' --output-on-failure
```

Expected: runtime open and Plan admission currently accept the non-trivial graph.

- [x] **Step 3: Implement graph-wide admission**

Make the internal helper validate graph/subgraph/node descriptors without
allocation. In `cflow_run_open_subgraph()`, validate the actual Source type and
the complete Graph before lifecycle allocation or Source move. In Plan support
and compilation, reject the graph before allocating the dense successor index
or Plan implementation.

Use the diagnostic `"plan requires trivial value storage"` for Plan compilation.

- [x] **Step 4: Run focused tests and verify GREEN**

Run the Task 2 commands again. Expected: both tests pass.

### Task 2.5: Low-level resumable admission

**Files:**
- Modify: `cflow/src/coord.c`
- Modify: `cflow/src/subrun.c`
- Modify: `cflow/src/relation_exec.c`
- Modify: `cmeta/src/cmeta.c`
- Test: `cflow/tests/cflow_runtime_test.c`

- [x] **Step 1: Prove the exposed byte-storage constructors accept owning values**

Add RED tests for one-shot values, coordination children, and SubRun input.

- [x] **Step 2: Apply admission before allocation or ownership transfer**

Reject unsupported value types in the low-level constructors. Give the
coordinator event and `cmeta_type_size` their accurate trivial-storage
properties without adding Collector lifecycle callbacks to `size_t`.

- [x] **Step 3: Verify CFlow and CBind compatibility**

Run the WAIT conformance test and the CBind exact Collector-status test together.

### Task 3: Public contract documentation

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cflow/include/cflow/sources.h`
- Modify: `cflow/include/cflow/adapters.h`
- Modify: `cflow/README.md`

**Interfaces:**
- Produces: documented distinction between semantic data descriptors, borrowed Range traversal, and trivial byte-storage execution.
- Consumes: Task 1 and Task 2 behavior.

- [x] **Step 1: Clarify the existing contracts**

Document that `cmeta_container_data()` returns semantic shape and never raw
storage. Document trivial-storage admission on the affected CFlow source and
owned-byte-result APIs. Record that trait-aware non-trivial execution and a
versioned contiguous storage view are separate future contracts.

- [x] **Step 2: Compile C and C++ public-header consumers**

Run:

```powershell
cmake --build --preset win-release-user --target cmeta_header_cpp_test cflow_header_cpp_test
ctest --preset win-release-user -R '^(cmeta|cflow)_header_cpp_test$' --output-on-failure
```

Expected: both public-header tests pass.

### Task 4: Regression and delivery verification

**Files:**
- Modify: `docs/superpowers/plans/2026-08-24-cflow-trivial-value-admission.md` (mark completed steps)

**Interfaces:**
- Consumes: all prior tasks.
- Produces: a clean, reviewable commit with reproducible evidence.

- [x] **Step 1: Run the closest regression set**

Build and run runtime, pipeline, graph, direct, scheduler compatibility, and
execution tests through `win-release-user`.

- [x] **Step 2: Run fresh full Release verification**

Run fresh configure, full build, full CTest, and install preset under
`VsDevCmd.bat`.

- [x] **Step 3: Audit the diff**

Run `git diff --check`, search changed production/test files for forbidden
placeholders, sync CodeGraph, and confirm `.codegraph`, build outputs, and
vcpkg outputs are not tracked.

- [ ] **Step 4: Commit when requested**

Commit with:

```text
fix(cflow): reject nontrivial byte storage
```
