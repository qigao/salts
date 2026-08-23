# CFlow Plan Dense Successor Index Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make validation and compilation of an already normalized linear CFlow
Graph use linear topology work without changing public Graph or Plan layouts.

**Architecture:** Add a private, phase-owned dense successor view built from the
flat edge fact source. Use it in Graph validation and Plan compilation, and
preallocate the exact Plan instruction tape. Retain flat edges as the public IR.

**Tech Stack:** C11, CMake Presets, TinyTest, MSVC Release.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-plan-dense-index-design.md`

## Task 1: Establish the baseline and private-index contract

**Files:**

- Inspect: `cflow/src/graph.c`
- Inspect: `cflow/src/plan_compile.c`
- Inspect: `cflow/include/cflow/plan_internal.h`
- Inspect: `cflow/tests/cflow_graph_test.c`
- Inspect: `cflow/tests/cflow_pipeline_test.c`

- [x] Synchronize CodeGraph and inspect the validation/compile callers.
- [x] Configure MSVC Release with tests and benchmarks enabled.
- [x] Build and run the unchanged focused Graph and Plan tests.
- [x] State ownership, lifetime, complexity and failure invariants in the spec.

## Task 2: Add failing private-index tests

**Files:**

- Create: `cflow/src/dense_successor_index.h`
- Create: `cflow/tests/cflow_dense_successor_index_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

- [x] Define the private index result and ownership contract.
- [x] Test edge-order-independent successor lookup and terminal nodes.
- [x] Test deterministic fan-out detection and invalid edge rejection.
- [x] Test that destroy/reset leaves a reusable zero state.
- [x] Build the new target and confirm RED because implementation is absent.

## Task 3: Implement and integrate the dense index

**Files:**

- Create: `cflow/src/dense_successor_index.c`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/src/graph.c`
- Modify: `cflow/src/plan_compile.c`

- [x] Implement checked, transactional `Theta(V + E)` construction/destruction.
- [x] Replace validation edge rescans and recursive DFS with the derived view.
- [x] Preserve validation diagnostic precedence and fail-fast OOM behavior.
- [x] Reuse one root index for Plan support, instruction count and compilation.
- [x] Allocate the exact instruction tape once and preserve cleanup semantics.
- [x] Run the new unit test and focused Graph/Plan tests until GREEN.

## Task 4: Add normalized Graph-to-Plan Release evidence

**Files:**

- Modify: `cflow/benchmarks/cflow_graph_path_benchmark.c`

- [x] Add correctness-checked compilation samples for boundary, typical and
  peak operator counts.
- [x] Keep Graph construction/normalization outside timing and Plan lifecycle
  inside timing.
- [x] Build and run `cflow_direct_benchmark` in MSVC Release.
- [x] Record reproducible local results without introducing a timing gate.

## Task 5: Verify and deliver

**Files:**

- Inspect: all changed production, test, benchmark and design files.

- [x] Build all CFlow test and benchmark targets in MSVC Release.
- [x] Run the complete CFlow test set with output on failure.
- [x] Inspect public headers, ABI-sensitive structs and the final diff.
- [ ] Commit, push and open a new PR stacked on PR #44's branch.
