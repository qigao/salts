# CFlow Plan Filter/Map Memory Fusion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute eligible complete Filter/Map Plans with borrowed input, exact
survivor-sized stages and no materialized input copy.

**Architecture:** Plan compilation records whether the complete immutable
instruction sequence satisfies the value-fusion contract. Evaluation uses a
selection bit vector and exact survivor-sized Map-stage buffers for eligible
Plans, while all other semantics retain the existing materialized evaluator.

**Tech Stack:** ISO C11, CMeta effects/properties, CFlow Plan, TinyTest, CMake
Presets, MSVC `/O2`, Clang `-O3`.

**Spec:**
`docs/superpowers/specs/2026-08-23-cflow-plan-filter-map-memory-fusion-design.md`

## Global Constraints

- Preserve all public CMeta/CFlow APIs, ABI, result ownership and error returns.
- Fuse only complete leading-Filter/Map Plans whose callbacks are pure,
  deterministic, total and no-alias.
- Never fall back after fused execution starts or a callback/allocation fails.
- Keep all mutable selection/intermediate/result storage evaluation-local.
- Use checked arithmetic for selection, intermediate and result capacity.
- Retain the materialized evaluator for every ineligible Plan.

---

### Task 1: Resource contract RED tests

**Files:**
- Modify: `cflow/include/cflow/plan_internal.h`
- Modify: `cflow/tests/cflow_pipeline_test.c`

**Interfaces:**
- Consumes: existing `cflow_plan_eval_array` and private `cflow_plan_impl`.
- Produces: private `cflow_plan_eval_stats` and
  `cflow_plan_eval_array_profile(const cflow_plan *, const void *, size_t,
  cflow_result *, cflow_plan_eval_stats *)` declaration used by Task 3.

- [x] **Step 1: Add a failing fixed-resource test.** Declare the private profile
  type/function and evaluate the existing six-item value pipeline. Assert the
  literal output plus `fused_value_path == true`, three allocation calls, one
  selection byte, an exact three-value `long` intermediate, 24 result bytes,
  exact allocated/peak totals and zero staged input-copy bytes.
- [x] **Step 2: Add a failing stateful fallback test.** Define one stateful
  `int -> long` Map, compile/evaluate it through the profile entry point, assert
  correct values and `fused_value_path == false`.
- [x] **Step 3: Run `cflow_pipeline_test` under `win-release-user`.** Expected
  RED: link failure for the declared but not implemented profile entry point.

### Task 2: Compile immutable fusion eligibility

**Files:**
- Modify: `cflow/include/cflow/plan_internal.h`
- Modify: `cflow/src/plan_compile.c`
- Test: `cflow/tests/cflow_pipeline_test.c`

**Interfaces:**
- Consumes: `cflow_plan_call`, `cmeta_effects_are_pure`,
  `cmeta_properties_include` and `CMETA_PROP_STABLE`.
- Produces: `cflow_plan_impl.fused_value`, `fused_filter_count`,
  and `fused_map_call_count`.

- [x] **Step 1: Implement `call_is_fusible_value`.** Require pure effects and
  `CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS` on the bound callable.
- [x] **Step 2: Implement `prepare_fused_value`.** Scan the completed instruction
  array, reject non-Filter/Map operations and Filters after the first Map, count
  callbacks with overflow checks.
- [x] **Step 3: Invoke eligibility preparation once after compilation.** It is a
  non-failing optimization decision; an ineligible shape leaves the flag false.
- [x] **Step 4: Build `cflow_pipeline_test`.** It must still be RED because the
  profile evaluator is not implemented, proving Task 1 remains active.

### Task 3: Bounded fused evaluator

**Files:**
- Modify: `cflow/src/plan_exec.c`
- Test: `cflow/tests/cflow_pipeline_test.c`

**Interfaces:**
- Consumes: Task 2 eligibility fields.
- Produces: implemented `cflow_plan_eval_array_profile`; public
  `cflow_plan_eval_array` delegates with a null stats pointer.

- [x] **Step 1: Add checked helpers.** Implement overflow-safe selection byte
  count, addition and multiplication without VLA/alloca.
- [x] **Step 2: Allocate the bounded selection vector.** Skip it when there are
  no Filters or inputs and account for the exact request.
- [x] **Step 3: Run leading Filters stage-by-stage.** Initialize selection to all
  inputs, clear rejected bits, count survivors and abort transactionally on any
  callback failure.
- [x] **Step 4: Allocate exact result storage.** Check
  `selected_count * output_type->size`; return null data for zero survivors.
- [x] **Step 5: Execute Maps stage-by-stage.** Allocate every Map output for the
  exact survivor count, keep indirect callback targets stable inside hot loops,
  release the previous stage after commit and copy selected input only for a
  Filter-only Plan.
- [x] **Step 6: Fill exact fused stats and clean every exit.** Set profile fields
  from requested allocation sizes, free auxiliary/result on failure and commit
  result only after complete success.
- [x] **Step 7: Preserve materialized fallback.** Move the current evaluator to
  a helper and use it unchanged when `fused_value == false`.
- [x] **Step 8: Run `cflow_pipeline_test`.** Expected GREEN with the resource and
  stateful fallback assertions passing.

### Task 4: Boundary coverage and measurement

**Files:**
- Modify: `cflow/tests/cflow_pipeline_test.c`
- Modify: `cflow/benchmarks/cflow_direct_benchmark.c`
- Update: `docs/superpowers/specs/2026-08-23-cflow-plan-filter-map-memory-fusion-design.md`

**Interfaces:**
- Consumes: private profile evaluator from Task 3.
- Produces: boundary regression tests and benchmark resource evidence.

- [x] **Step 1: Add empty/all-filtered/map-only/filter-only tests.** Assert owned
  result count/type/data semantics and exact fused path selection without
  inspecting implementation source text.
- [x] **Step 2: Validate benchmark resource accounting outside timing.** Assert
  128 selection bytes, 2,048 intermediate bytes, 4,096 result bytes, three
  allocations, 6,272 allocated bytes, 6,144 peak bytes and zero staged
  input-copy bytes on Win64.
- [x] **Step 3: Run focused MSVC and Clang tests.** Build and execute pipeline,
  Direct and calculus-conformance targets using the repository presets.
- [x] **Step 4: Run all 14 CMeta/CFlow CTest targets in both Release trees.** No
  test failure is accepted; record unrelated existing compiler warnings.
- [x] **Step 5: Run five 50,000-sample benchmark executions per compiler.**
  Calculate median and range for Direct, stage-fused Plan and an identical
  materialized Plan control, then enforce the paired 5% regression gate.
- [x] **Step 6: Run `git diff --check`, CodeGraph affected analysis and diff
  review.** Verify no installed/public header changed and no shared mutable
  evaluation state was introduced.
- [x] **Step 7: Commit and push the branch.** Stage only the implementation,
  tests, benchmark and phase documents; update Draft PR #35 through the explicit
  GitHub CLI path when authentication is available.
