# CMeta/CFlow Canonical Raw Batch Dispatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development or superpowers:executing-plans to
> implement this plan task-by-task.

**Goal:** Preserve callable semantics while moving eligible Plan Filter/Map
dispatch from one erased adapter call per item to one predecoded batch call per
stage.

**Architecture:** CMeta owns an explicit immutable canonical-raw capability and
validates it at bind time. CFlow Plan predecodes signature-specific unary batch
loops at compile time; noncanonical custom/capturing callables retain the adapter
path. Existing fused ownership and failure behavior remain unchanged.

**Tech Stack:** ISO C11, CMeta finite signature policy, CFlow Plan, TinyTest,
CMake Presets, MSVC `/O2`, Clang `-O3`.

**Spec:**
`docs/superpowers/specs/2026-08-23-cmeta-cflow-canonical-raw-batch-dispatch-design.md`

## Global Constraints

- The public ABI change is authorized, but existing initializer call sites must
  default to adapter-safe behavior.
- Never infer canonical raw dispatch from capture size, raw target presence,
  effects or properties.
- Validate contradictions during bind/compile; do not silently downgrade.
- Preserve custom adapter side effects, capturing callable semantics, exact
  allocation counts/bytes, order and owned result behavior.

### Task 1: CMeta capability RED/GREEN

**Files:**
- Modify: `cmeta/tests/cmeta_core_test.c`
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/src/cmeta.c`

- [x] Add behavior tests for named canonical raw, adapter default, invalid tag,
  capture rejection and active raw-target equality. Build `cmeta_core_test` and
  record the expected compile/link RED before production declarations exist.
- [x] Add explicit dispatch tag values, a source-compatible adapter initializer,
  the canonical initializer and capability query.
- [x] Mark only generated named typed factories canonical. Validate tag and
  canonical invariants in `cmeta_callable_bind`; extend equality only where raw
  dispatch makes target identity observable.
- [x] Build/run the focused CMeta test to GREEN.

### Task 2: CFlow Plan batch RED/GREEN

**Files:**
- Modify: `cflow/tests/cflow_pipeline_test.c`
- Modify: `cflow/tests/cflow_direct_test.c`
- Modify: `cflow/include/cflow/meta.h`
- Modify: `cflow/include/cflow/plan_internal.h`
- Modify: `cflow/src/plan_compile.c`
- Modify: `cflow/src/plan_exec.c`

- [x] Add profile assertions: ordinary Filter/Map performs three raw batch stage
  calls and zero adapter item calls; custom zero-capture adapter performs three
  adapter calls; capturing Map performs adapter calls and produces literal output.
  Build focused tests and record RED before implementation.
- [x] Switch only generated named CFlow typed factories to canonical raw.
- [x] Generate one internal batch loop per enabled unary signature, preserving
  byte-boundary alignment with `memcpy`; expose a private signature resolver.
- [x] Predecode the batch loop in Plan compilation only for validated canonical
  callables. Invoke it once per fused Filter/Map stage; otherwise keep the exact
  adapter loops. Populate private profile counters.
- [x] Build/run focused CFlow tests to GREEN and confirm existing allocation
  assertions remain unchanged.

### Task 3: Compatibility and regression

**Files:**
- Verify: CMeta/CFlow C and C++ header tests and adjacent execution tests.

- [x] Build the relevant Release targets with repository presets.
- [x] Run the complete CMeta/CFlow Release CTest subset under MSVC and Clang.
- [x] Run `git diff --check`, CodeGraph affected analysis and inspect ABI-facing
  diffs for accidental behavior changes.

### Task 4: Performance evidence

**Files:**
- Update this plan/spec only with measured results if documentation is needed.

- [x] Run five 50,000-sample benchmark executions on MSVC and Clang Release.
- [x] Report medians/ranges for Direct, raw staged control, materialized Plan and
  optimized Plan. Compare optimized Plan with its pre-change medians and raw
  ceiling; explain any >10% regression or unexpected memory change.

### Task 5: Delivery

- [x] Self-review all changes and focused mutation risks.
- [ ] Commit only scoped files with a descriptive commit and push the feature
  branch for Draft PR #35.
