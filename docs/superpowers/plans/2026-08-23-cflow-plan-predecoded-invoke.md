# CFlow Plan Predecoded Invoke Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove repeated generic signature dispatch from ordinary typed
Filter/Map Plan callbacks while preserving custom callable semantics.

**Architecture:** Generate signature-specific adapters from CMeta's existing
signature policy and cache each callable's authoritative invoke pointer and type
descriptors in immutable private Plan instructions.

**Tech Stack:** ISO C11, CMeta generated signatures, CFlow Plan, TinyTest, CMake
Presets, MSVC `/O2`, Clang `-O3`.

**Spec:**
`docs/superpowers/specs/2026-08-23-cflow-plan-predecoded-invoke-design.md`

## Constraints

- Preserve public CMeta/CFlow structs, functions and ownership contracts.
- Never bypass a capturing, bound or custom callable's authoritative adapter.
- Keep the Plan immutable and safe for concurrent read-only evaluation.
- Keep memory fusion and reusable scratch outside this phase.
- Require measured improvement; do not accept source-level intuition alone.

## Task 1: Specify callback predecode with a failing test

**Files:**

- Modify: `cflow/tests/cflow_pipeline_test.c`
- Modify: `cflow/tests/CMakeLists.txt` only if private-header access requires it
- Inspect: `cflow/include/cflow/plan_internal.h`

- [x] Add a focused test that compiles the existing Filter/Map pipeline and
  asserts that each private instruction committed its invoke entry and exact
  input/output descriptors.
- [x] Build and run `cflow_pipeline_test`; record the expected RED failure before
  production changes.

## Task 2: Generate signature-specific typed adapters

**Files:**

- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cflow/include/cflow/meta.h`
- Test: focused CMeta tests and `cflow_direct_test`

- [x] Generate unary and binary adapters from `CMETA_ALL_SIGNATURES`, preserving
  current input/output/null semantics.
- [x] Select the appropriate generated adapter for ordinary CFlow `typed(...)`
  functions without changing capture/bind macros.
- [x] Build and run focused CMeta callable tests and Direct tests.

## Task 3: Compile and execute immutable call records

**Files:**

- Modify: `cflow/include/cflow/plan_internal.h`
- Modify: `cflow/src/plan_compile.c`
- Modify: `cflow/src/plan_exec.c`
- Test: `cflow/tests/cflow_pipeline_test.c`

- [x] Add a private compiled-call record owning the callable, cached invoke
  entry and validated input/output descriptors.
- [x] Prepare all Filter/Map call records during Plan compilation and clean up
  partial arrays on failure.
- [x] Execute Filter/Map through the cached authoritative entry without runtime
  signature lookup.
- [x] Run the RED test to GREEN and confirm custom-adapter regression coverage.
  Record the existing capture/partial-application admission blocker separately;
  do not weaken callable validation inside this performance change.

## Task 4: Measure and verify

**Files:**

- Inspect: `cflow/benchmarks/cflow_direct_benchmark.c`
- Update measured evidence in:
  `docs/superpowers/specs/2026-08-23-cflow-plan-predecoded-invoke-design.md`

- [x] Run five MSVC Release benchmark samples and calculate median/range.
- [x] Run five Clang Release benchmark samples and calculate median/range.
- [x] Compare Direct and Plan against the recorded investigation baseline and
  enforce the 30% Plan gain / 5% Direct regression gates.
- [x] Inspect optimized object code for removed ordinary typed
  `cmeta_fn_invoke` calls.
- [x] Run focused tests followed by all `^cflow_` tests in the affected Release
  trees.
- [x] Run formatting and diff review, then record facts, calculations and
  remaining memory-path work.
