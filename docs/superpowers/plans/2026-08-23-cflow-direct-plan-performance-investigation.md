# CFlow Direct/Plan Performance Investigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Explain and quantify the Direct/Plan performance gap, including memory costs, without changing production APIs or executor semantics.

**Architecture:** Keep the production executors untouched and add benchmark-only controlled paths that reproduce one cost dimension at a time. Validate full observational equivalence before timing, then correlate benchmark deltas with optimized code and CPU samples.

**Tech Stack:** ISO C11, CMeta typed callables, CFlow Direct/Plan, TinyTest benchmarks, CMake Presets, MSVC and Clang Release, LLVM objdump and Windows Performance Recorder.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-direct-plan-performance-investigation-design.md`

## Constraints

- Preserve all public CFlow/CMeta APIs, ABI, ownership and error behavior.
- Use input items per second as the common throughput unit.
- Keep setup and semantic validation outside timed regions.
- Include ownership-mandated allocation and destruction inside the measured path.
- Do not infer a production optimization from a single compiler or sample.

## Task 1: Establish a reproducible baseline

**Files:**

- Inspect: `cflow/benchmarks/cflow_direct_benchmark.c`
- Inspect: `cflow/src/plan_exec.c`
- Inspect: `cmeta/src/cmeta_callable.c`

- [x] Build the unchanged benchmark in MSVC Release and Clang Release.
- [x] Run at least five samples per compiler and record Direct/Plan median and
  range.
- [x] Confirm optimization and link-time optimization flags from generated build
  commands.
- [x] Calculate the fixed pipeline's callable dispatch count, allocations,
  copied bytes, materialized bytes and peak live bytes from source invariants.

## Task 2: Add benchmark-only cost decomposition

**Files:**

- Modify: `cflow/benchmarks/cflow_direct_benchmark.c`
- Modify if needed: `cflow/benchmarks/cflow_direct_benchmark_consume.c`

- [x] Add complete-output validation for each proposed controlled path before
  adding its timed benchmark.
- [x] Add a fused typed owned-output path to isolate final allocation/free cost.
- [x] Add a Plan-shaped staged typed path to isolate copy, materialization and
  allocation/free costs.
- [x] Add a fused erased-callable caller-buffer path to isolate erased dispatch.
- [x] Label all throughput as input items and keep the external consumer in each
  timed sample.

## Task 3: Verify benchmark validity and locate CPU cost

**Files:**

- Inspect: generated build commands and optimized executable/object files
- Inspect: `cflow/include/cflow/direct.h`
- Inspect: `cflow/src/plan_exec.c`

- [x] Rebuild and run all controlled paths with MSVC Release and Clang Release.
- [x] Inspect optimized machine code for Direct fusion/vectorization and Plan
  indirect dispatch/materialization boundaries.
- [x] Capture a CPU sample profile when the available Windows tooling can produce
  a non-interactive trace; otherwise record the exact tooling limitation.
- [x] Compare medians and attribute only deltas supported by the controlled paths
  and code evidence.

## Task 4: Regression verification and recommendation

**Files:**

- Modify if findings require clarification:
  `docs/superpowers/specs/2026-08-23-cflow-direct-plan-performance-investigation-design.md`

- [x] Run `cflow_direct_test`.
- [x] Run all `^cflow_` tests in the affected Release tree.
- [x] Review the diff for production API/behavior changes and benchmark
  dead-code-elimination hazards.
- [x] Report facts, calculations and inferences separately, including memory
  optimization candidates, compatibility risks and the next measurable gate.
