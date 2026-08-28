# CFlow Compiled Managed Values Phase 1 Implementation Plan

> **Execution:** Implement inline in this worktree. The user explicitly requested no sub-agents.

**Goal:** Extend sequential materialized `cflow_plan` execution to safely own CMeta managed values while preserving explicit rejection for fused, parallel, relation, and branching paths that do not yet have a lifecycle proof.

**Architecture:** CMeta type descriptors remain the lifecycle fact source. A compiled materialized vector owns one contiguous allocation plus a live prefix; every successful copy, move, callable invocation, or generator yield adds exactly one live element, and every removal or failure cleanup destroys it exactly once. `cflow_result` receives the vector only after full success and releases managed elements through its existing destroy entry point. Caller input remains borrowed.

**Tech Stack:** C11, CMeta traits, CFlow Plan, TinyTest, CMake presets.

---

### Task 1: Specify the lifecycle result contract with failing tests

**Files:**
- Create: `cflow/tests/cflow_plan_managed_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

1. Add a managed test value whose copy/move/destroy callbacks count live resources and can inject a copy failure.
2. Add a source-only compiled Plan test proving input is independently copy-constructed and result destruction releases every Plan-owned value once.
3. Add `take` and `skip` tests proving discarded values are destroyed without affecting borrowed inputs.
4. Add a copy-failure test proving partial construction is cleaned and the public result remains zero.
5. Build and run only `cflow_plan_managed_test`; confirm the current trivial-storage admission makes the new success tests fail before implementation.

### Task 2: Add aligned managed result storage

**Files:**
- Create: `cflow/src/result_storage.h`
- Modify: `cflow/src/value_storage.h`
- Modify: `cflow/src/adapters.c`

1. Add checked allocation helpers that return ordinary storage for trivial values and an aligned payload with a recoverable allocation header for managed values.
2. Add one move-construction helper shared by value slots and materialized vectors.
3. Make `cflow_result_destroy()` destroy each managed result element before releasing its backing allocation; preserve the existing trivial-result `free()` behavior.
4. Keep zero results idempotent and require no new public struct fields.

### Task 3: Convert materialized Plan vectors to a live-prefix state machine

**Files:**
- Modify: `cflow/include/cflow/plan_internal.h`
- Modify: `cflow/src/plan_exec.c`

1. Add allocation and capacity metadata to the internal value vector.
2. Replace byte-copy initialization with per-element copy construction and exact partial cleanup.
3. Implement filter and skip transactionally into a new vector so callable/copy failure leaves the original live prefix intact for cleanup.
4. Destroy removed tails for take; implement map outputs as successful-construction prefixes.
5. Grow flat-map output by allocating a new aligned vector and move-constructing the complete live prefix before releasing old storage.
6. Implement reduce with lifecycle-aware accumulator and temporary slots.
7. Transfer the completed vector to `cflow_result` only after output type validation; otherwise destroy the full live prefix.

### Task 4: Expand compile admission without widening unsafe optimized paths

**Files:**
- Modify: `cflow/src/plan_compile.c`
- Modify: `cflow/include/cflow/plan.h`
- Modify: `cflow/include/cflow/adapters.h`

1. Admit Graph value types that are either trivial or provide COPY/MOVE/DESTROY.
2. Require trivial input and output for fused-value admission.
3. Require an entirely trivial compiled graph for parallel-reduce admission.
4. Update public documentation: sequential materialized Plans may return managed values; byte-result adapters still reject managed Graphs; unsupported topology remains fail-fast.

### Task 5: Verify behavior and compatibility

**Files:**
- Modify if needed: `cflow/tests/cflow_header_cpp_test.cpp`

1. Run `cflow_plan_managed_test`.
2. Run adjacent Plan suites: pipeline, stream slicing, direct, parallel reduce, certificate, calculus conformance, and C++ header tests.
3. Run the full CFlow test label/set through the documented Windows Release preset.
4. Run the same focused tests under the Windows development/ASan preset when available.
5. Inspect `git diff --check`, final diff, and status; do not claim Linux/macOS/Android validation unless CI or the configured remote Linux host has actually run it.

### Deferred #135 topology phase

Branching, relation, and general parallel topology remain rejected in this phase. The next phase will classify ownership per edge, choose a first topology with single-owner result slots, and add cancellation/finalization proofs before enabling it. No implicit interpreter fallback is introduced.
