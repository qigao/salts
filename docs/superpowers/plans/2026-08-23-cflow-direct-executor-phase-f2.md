# CFlow Direct Executor Phase F-2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first production CFlow Direct executor: a generated, fused C11 Filter/Map loop with one memory pass, no execution-time allocation and zero erased callback dispatches.

**Architecture:** A named compile-time schema is replayed into both a Surface Graph builder and a translation-unit-local Direct evaluator. Public inline validation enforces the closed synchronous value contract and bounded-buffer protocol before the hot loop; existing Plan and Kernel paths remain independent.

**Tech Stack:** ISO C11, CMeta schema/typed callable macros, CFlow Graph/Plan/Kernel, TinyTest, CMake Presets, MSVC Release.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-direct-executor-phase-f2-design.md`

## Global Constraints

- Preserve existing Graph, Plan and Kernel public behavior and ABI.
- Accept only named non-capturing `value` Filter/Map rows in Phase F-2.
- Do not allocate, schedule or perform erased callback invocation in Direct.
- Reject unsupported input explicitly; never fall back to Plan or Kernel.
- Keep all generated functions translation-unit-local.
- Use a caller-provided bounded output array and checked, disjoint memory ranges.
- Develop each behavior test-first and run the focused executable after every
  RED/GREEN cycle.

---

## Task 1: Establish the public Direct contract with a failing test

**Files:**

- Create: `cflow/tests/cflow_direct_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

- [x] Add a same-TU `value` Filter/Map pipeline schema and instantiate
  `cflow_direct_pipeline`.
- [x] Add a TinyTest case that calls the generated Surface builder and compares
  Direct output `{2.0, 8.0, 18.0}` with Plan and Kernel for input
  `{1, 2, 3, 4, 5, 6}`.
- [x] Register `cflow_direct_test` as an ISO C11 target.
- [x] Build only `cflow_direct_test` and record the expected RED compile failure
  because `cflow/direct.h` or `cflow_direct_pipeline` does not exist.

## Task 2: Implement the minimum generated Filter/Map path

**Files:**

- Create: `cflow/include/cflow/direct.h`
- Modify: `cflow/include/cflow/cflow.h`

- [x] Add `cflow_direct_status` with success, invalid argument, ineligible and
  capacity-exceeded values.
- [x] Add inline callable/type eligibility helpers using CMeta metadata and
  trivial-copy/trivial-destroy trait requirements.
- [x] Add schema row replay macros for eligibility, Surface Graph construction
  and direct Filter/Map statements.
- [x] Add `cflow_direct_pipeline(name, input_type, input_desc, output_type,
  stage_count, schema)` generating an automatically indexed stage chain and:
  `name_eligible`, `name_build` and `name_eval_array`.
- [x] Include the new header from `cflow/cflow.h`, with C-only DSL macros hidden
  from C++.
- [x] Rebuild and run `cflow_direct_test`; require GREEN.

## Task 3: Prove bounded ownership and fail-fast behavior

**Files:**

- Modify: `cflow/tests/cflow_direct_test.c`
- Modify: `cflow/include/cflow/direct.h`

- [x] Add a capacity test requiring `CFLOW_DIRECT_CAPACITY_EXCEEDED`, zero result
  count and unchanged output sentinels.
- [x] Run the focused test and observe RED before adding capacity preflight.
- [x] Implement checked multiplication and range-disjoint preflight before any
  output write; keep the iteration free of a capacity branch.
- [x] Add empty-input, null-pointer, arithmetic-overflow and overlapping-buffer
  cases one at a time, observing RED then GREEN for each new behavior.

## Task 4: Prove eligibility and zero erased dispatch

**Files:**

- Modify: `cflow/tests/cflow_direct_test.c`
- Modify: `cflow/include/cflow/direct.h`

- [x] Add a named stateful callable schema and assert both generated eligibility
  and evaluation reject it before writes.
- [x] Add a same-TU trap callable whose direct function computes correctly while
  its erased invoke adapter increments a counter and fails.
- [x] Assert Direct produces the expected result and the erased-adapter counter
  remains zero.
- [x] Assert the generated Surface Graph uses the ordinary erased callable path,
  demonstrating that both forms come from the same schema but execute through
  distinct mechanisms.

## Task 5: Add isolated performance evidence

**Files:**

- Create: `cflow/benchmarks/CMakeLists.txt`
- Create: `cflow/benchmarks/cflow_direct_benchmark.c`
- Create: `cflow/benchmarks/cflow_direct_benchmark_consume.c`
- Modify: `cflow/CMakeLists.txt`

- [x] Gate the benchmark directory with the existing `BUILD_BENCHMARKS` option.
- [x] Build one fixed `value` Filter/Map schema and prepare identical input for
  Direct and Plan outside timed regions.
- [x] Measure complete Direct and Plan evaluations with `benchmark_ops`; include
  Plan result destruction in its timed operation because it is part of that
  executor's allocation-owning contract.
- [x] Validate count and full output outside timing, then run the benchmark
  in MSVC Release and retain the observed numbers in the handoff.

## Task 6: Verify compatibility and affected behavior

**Files:**

- Modify if required by findings: `cflow/include/cflow/direct.h`
- Modify if required by findings: `cflow/tests/cflow_direct_test.c`

- [x] Build and run `cflow_direct_test`.
- [x] Build and run all `^cflow_` tests, including the C++ aggregate-header test.
- [x] Configure/build the benchmark-enabled tree and run
  `cflow_direct_benchmark`.
- [x] Sync CodeGraph and inspect affected symbols/files for missing callers or
  regression candidates.
- [x] Review the diff for unsupported fallback, hot-loop indirect calls,
  execution-time allocation, public-header portability and user-owned changes.
- [x] Commit, push the stacked branch and open a Draft PR targeting
  `test/cflow-calculus-conformance-phase-f1`.
