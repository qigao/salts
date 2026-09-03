# CFlow Ordered Parallel Reduce Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an explicit Plan path that evaluates eligible associative reductions on a borrowed concurrent Executor while preserving encounter order.

**Architecture:** Reuse the sequential Plan prefix evaluator, split its owned values into contiguous chunks, submit one reduction task per chunk, and merge partials by chunk index. A per-evaluation latch owns completion; submission or callback failure aborts transactionally without serial fallback.

**Tech Stack:** C11, CMeta callable metadata, CFlow Plan IR, Salts Executor, TinyTest benchmarks.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

**Status:** Complete. Implementation, failure closure, ASan/Release verification, local benchmark evidence, and workflow artifact publication are covered by this plan.

## Global Constraints

- Depends on the bounded-admission plan.
- Parallel execution is opt-in; `cflow_plan_eval_array()` remains sequential.
- Reassociation preserves chunk order and requires `PURE|TOTAL|ASSOCIATIVE|NO_ALIAS`.
- No partial result is visible on failure.
- The borrowed executor and input remain alive until the call returns.
- Do not add commutativity as a hidden requirement.

---

### Task 1: Encode eligibility in immutable Plan metadata

**Files:**
- Modify: `cflow/include/cflow/plan.h`
- Modify: `cflow/include/cflow/plan_internal.h`
- Modify: `cflow/src/plan_compile.c`
- Modify: `cflow/src/property.c`
- Test: `cflow/tests/cflow_direct_test.c`

**Interfaces:**
- Produces: `cflow_plan_parallel_reduce_supported()` and private terminal-reducer metadata.

- [x] Write failing positive and negative eligibility tests for complete premises, missing `ASSOCIATIVE`, missing `NO_ALIAS`, nonterminal reduce, and relation topology.
- [x] Run `cflow_direct_test` and confirm RED because the query does not exist.
- [x] During Plan compile, retain the terminal reducer instruction index and a boolean derived only from callable properties and supported linear topology. Do not infer algebraic laws from a C function pointer.
- [x] Run optimizer, Plan, and calculus conformance tests; verify an unsupported plan still fails without fallback.
- [x] Commit as `feat(cflow): encode parallel reduce eligibility`.

### Task 2: Transactional ordered execution

**Files:**
- Create: `cflow/src/plan_parallel_reduce.c`
- Modify: `cflow/include/cflow/plan.h`
- Modify: `cflow/src/plan_exec.c`
- Modify: `cflow/CMakeLists.txt`
- Test: `cflow/tests/cflow_parallel_reduce_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cflow_executor_try_post()` and compiled reducer metadata.
- Produces: `cflow_plan_eval_array_with_options()`.

- [x] Add a TinyTest executable covering empty, one item, exact chunk size, remainder chunk, and repeated evaluation. Use the `long` left-projection reducer `combine(left, right) = left`, which is associative but noncommutative, and assert encounter order without adding a new signature family.
- [x] Run the new target and confirm RED because options and executor code are absent.
- [x] Implement checked task-count calculation: `min(max_tasks, ceil(count/min_items_per_task))`, with `task_count >= 2`; reject zero options and overflow.
- [x] Allocate one evaluation frame containing mutex/condition, descriptors, partial slots, completion/failure counters, and the owned prefix buffer. Each task writes one slot and publishes completion under the latch.
- [x] Submit with checked nonblocking admission. On the first rejection, stop submitting, wait only for accepted tasks, destroy every initialized partial, and return false with `out` unchanged.
- [x] Merge successful partials in ascending chunk index with the same reducer. Commit exactly one owned result only after every combine succeeds.
- [x] Run the focused test and all Plan/runtime conformance tests.
- [x] Commit as `feat(cflow): execute ordered parallel reduce plans`.

### Task 3: Failure, shutdown, and concurrency verification

**Files:**
- Modify: `cflow/tests/cflow_parallel_reduce_test.c`
- Modify: `cflow/tests/cflow_runtime_test.c`

- [x] Add injectable reducer failures before and after other chunks complete; assert one reported failure, zero committed output, and balanced task completion.
- [x] Saturate a bounded executor so partial submission fails; assert no fallback and no use-after-return by accepted tasks.
- [x] Trigger executor shutdown during evaluation; assert `CLOSED`, completed cleanup, and no hang.
- [x] Run Windows ASan/development CFlow tests, then Windows Release CFlow tests.
- [x] Commit as `test(cflow): cover parallel reduce failure closure`.

### Task 4: Benchmark and document the decision boundary

**Files:**
- Create: `cflow/benchmarks/cflow_parallel_reduce_benchmark.c`
- Modify: `cflow/benchmarks/CMakeLists.txt`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`
- Modify: `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

- [x] Add independently validated sequential and ordered-parallel results for 1 Ki, 64 Ki, and 1 Mi `long` values.
- [x] Use `benchmark_ops`; keep executor construction and input preparation outside the timed block. Report items reduced per sample and worker/task configuration.
- [x] Run five local Release samples. Reject the patch if the untouched sequential row regresses more than 10%; report parallel crossover without claiming a universal speedup.
- [x] Add the benchmark target to host evidence artifacts without a shared-runner performance gate.
- [x] Run full CFlow CTest, both CFlow benchmarks once, and `git diff --check`.
- [x] Commit as `perf(cflow): benchmark ordered parallel reduce`.
