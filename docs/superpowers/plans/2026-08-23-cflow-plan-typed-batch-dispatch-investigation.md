# CFlow Plan Typed Batch Dispatch Investigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Quantify the maximum Plan improvement available from raw typed batch
dispatch without changing production callable semantics.

**Architecture:** A benchmark-only fixed-pipeline control uses bound raw CMeta
function pointers with the same stage-fused ownership protocol as Plan. The
experiment produces an evidence gate; it does not change CMeta or CFlow APIs.

**Tech Stack:** ISO C11, CMeta typed callables, CFlow Plan, TinyTest benchmarks,
CMake Presets, MSVC `/O2`, Clang `-O3`.

**Spec:**
`docs/superpowers/specs/2026-08-23-cflow-plan-typed-batch-dispatch-investigation.md`

## Global Constraints

- Do not change public or production callable behavior.
- Do not infer raw-dispatch safety from zero capture size.
- Match Plan selection/intermediate/result ownership and checked arithmetic.
- Assert literal output parity before measuring.
- Use the existing 50,000-sample paired benchmark under both Release compilers.

---

### Task 1: Raw typed owned-path RED

**Files:**
- Modify: `cflow/benchmarks/cflow_direct_benchmark.c`

**Interfaces:**
- Consumes: the three bound benchmark `cmeta_callable.meta.call` members.
- Produces: benchmark-local
  `cflow_direct_bench_raw_staged_owned(const int *, size_t, double **, size_t *,
  cmeta_fn_U_I_B_t, cmeta_fn_U_I_L_t, cmeta_fn_U_L_D_t)`.

- [x] **Step 1: Declare the benchmark-local function without defining it.** Add
  an untimed call using the three raw bound targets and assert the hand-derived
  512-value result equals Plan output.
- [x] **Step 2: Build `cflow_direct_benchmark` with `win-release-user`.** Expected
  RED: the new function is declared and used but undefined.

### Task 2: Exact stage-owned raw control

**Files:**
- Modify: `cflow/benchmarks/cflow_direct_benchmark.c`

**Interfaces:**
- Consumes: Task 1 declaration and existing fixed benchmark input.
- Produces: one benchmark row named `Raw predecoded staged owned Filter/Map`.

- [x] **Step 1: Implement checked selection capacity and cleanup.** Use
  `count / 8 + (count % 8 != 0)`, reject multiplication overflow, initialize the
  bit vector, and keep one cleanup exit for selection/intermediate/result.
- [x] **Step 2: Execute stable raw stages.** Filter all inputs through the raw
  `int -> bool` pointer, allocate exact survivor storage, run the raw
  `int -> long` stage, release selection, allocate exact result, run the raw
  `long -> double` stage and release the intermediate.
- [x] **Step 3: Build and run the MSVC benchmark.** Expected GREEN: literal
  parity assertions and the new benchmark row pass.
- [x] **Step 4: Build and run the Clang benchmark.** Expected GREEN with the same
  parity assertions.

### Task 3: Evidence and decision

**Files:**
- Update:
  `docs/superpowers/specs/2026-08-23-cflow-plan-typed-batch-dispatch-investigation.md`

**Interfaces:**
- Consumes: Direct, raw-predecoded, stage-fused Plan and materialized Plan rows.
- Produces: measured medians/ranges and the public-capability decision.

- [x] **Step 1: Run five 50,000-sample executions per compiler.** Record raw and
  stage-fused Plan medians/ranges and calculate `raw / plan - 1`.
- [x] **Step 2: Apply the 20% gate.** Reject specialization below the gate; above
  the gate, document the upper bound and request authorization before any public
  callable capability or ABI change.
- [x] **Step 3: Run `git diff --check`, CodeGraph affected analysis and review.**
  Confirm only benchmark and investigation documents changed.
- [x] **Step 4: Commit and push the branch.** Stage only those three files.
