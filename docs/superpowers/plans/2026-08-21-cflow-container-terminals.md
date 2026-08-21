# CFlow Finite Container Terminals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend CFlow with structured statuses, parameterized finite-stream operators, injectable bounded-state protocols, and reusable Java-style terminal execution without depending on Container.

**Architecture:** Graphs store typed semantic parameters only. Evaluation receives optional sequence/set backend strategies and generic CMeta collectors; no backend is required for stateless pipelines, while unsupported stateful operations fail explicitly.

**Tech Stack:** ISO C11, CMeta callables/traits/Range/collector, CFlow graph/runtime/optimizer/plan, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- Execute `2026-08-21-cmeta-container-traits.md` first.
- CFlow must not include, link, name, or dynamically discover Container.
- No global backend registry, service locator, implicit interpreter fallback, or unbounded materialization.
- Graph owns semantic intent; evaluation owns transient state; source and backend interfaces are borrowed.
- Demand counts downstream values. Short-circuit terminals stop pulling and close the run.
- Preserve existing filter/map/flatMap/zip behavior and Surface/normalized/optimized parity.

---

### Task 1: Replace boolean collection failures with structured CFlow status

**Files:**
- Create: `cflow/include/cflow/status.h`
- Modify: `cflow/include/cflow/graph.h`
- Modify: `cflow/include/cflow/runtime.h`
- Modify: `cflow/include/cflow/stream.h`
- Modify: `cflow/include/cflow/adapters.h`
- Modify: `cflow/src/graph.c`
- Modify: `cflow/src/runtime.c`
- Modify: `cflow/src/stream.c`
- Modify: `cflow/src/adapters.c`
- Modify: `cflow/include/cflow/plan.h`
- Modify: `cflow/include/cflow/verify.h`
- Modify: `cflow/src/plan_exec.c`
- Modify: `cflow/src/verify.c`
- Modify: `cflow/examples/*.c`
- Modify: `cflow/tests/cflow_graph_test.c`
- Modify: `cflow/tests/cflow_pipeline_test.c`
- Create: `cflow/tests/cflow_status_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: current `const char *error` diagnostics.
- Produces: `cflow_status` and `cflow_stage` in `status.h`; `cflow_error` after `cflow_op` is declared in `graph.h`; first-error accessors and status-returning eval APIs.

- [ ] **Step 1: Write failing status/first-error tests**

```c
it("preserves the first stream build error") {
    cflow_stream stream = {0};
    check_null(cflow_stream_init(&stream, NULL));
    const cflow_error *error = cflow_stream_error(&stream);
    check_not_null(error);
    check_equal(error->status, CFLOW_INVALID_ARGUMENT);
    check_equal(error->stage, CFLOW_STAGE_BUILD);
}
```

Add terminal tests distinguishing `EMPTY`, `TYPE_MISMATCH`, `CAPACITY_EXCEEDED`, `OUT_OF_MEMORY`, `UNSUPPORTED`, and `EXECUTION_ERROR`.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cflow_status_test"
```

Expected: target/API missing.

- [ ] **Step 3: Add exact status and error types**

```c
/* status.h */
typedef enum cflow_status {
    CFLOW_OK = 0, CFLOW_EMPTY, CFLOW_INVALID_ARGUMENT,
    CFLOW_TYPE_MISMATCH, CFLOW_CAPACITY_EXCEEDED,
    CFLOW_OUT_OF_MEMORY, CFLOW_UNSUPPORTED, CFLOW_EXECUTION_ERROR
} cflow_status;

typedef enum cflow_stage {
    CFLOW_STAGE_BUILD, CFLOW_STAGE_NORMALIZE,
    CFLOW_STAGE_EXECUTE, CFLOW_STAGE_COLLECT
} cflow_stage;

/* graph.h, after cflow_op */
typedef struct cflow_error {
    cflow_status status;
    cflow_stage stage;
    cflow_op op;
    const char *message;
} cflow_error;
```

Use one helper that writes only when current status is `CFLOW_OK`. Convert `cflow_eval_array/stream` to return `cflow_status`; success comparisons become `== CFLOW_OK` throughout tests/examples.

- [ ] **Step 4: Run existing and new tests, then commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_status_test cflow_graph_test cflow_pipeline_test && ctest --preset win-release-user -R ""^cflow_(status|graph|pipeline)_test$"" --output-on-failure"
git add cflow
git commit -m "refactor(cflow): return structured execution status"
```

---

### Task 2: Add typed node parameters and injectable state backends

**Files:**
- Create: `cflow/include/cflow/backend.h`
- Modify: `cflow/include/cflow/operators.h`
- Modify: `cflow/include/cflow/graph.h`
- Modify: `cflow/include/cflow/runtime.h`
- Modify: `cflow/src/graph.c`
- Modify: `cflow/src/lower.c`
- Modify: `cflow/src/runtime.c`
- Create: `cflow/tests/cflow_backend_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: CMeta type traits and CFlow graph nodes.
- Produces: `cflow_node_params`, `cflow_sequence_state_ops`, `cflow_set_state_ops`, `cflow_eval_options`, `cflow_stream_from_range_with_options()`, and explicit backend admission.

- [ ] **Step 1: Write failing parameter-copy and no-backend tests**

Build a Surface graph with `distinct.max_unique = 4`, normalize it, and assert the value survives. Evaluate without a set backend and assert `CFLOW_UNSUPPORTED` before any source value is pulled.

- [ ] **Step 2: Run RED**

Build `cflow_backend_test`. Expected: typed parameters/backend options are missing.

- [ ] **Step 3: Define narrow interfaces and typed params**

```c
typedef union cflow_node_params {
    struct { size_t count; } limit;
    struct { size_t count; } skip;
    struct { size_t max_unique; } distinct;
    struct { size_t max_items; } sorted;
} cflow_node_params;

typedef struct cflow_set_state_ops {
    cflow_status (*open)(void **state, const cmeta_type_desc *type, size_t limit);
    cflow_status (*insert_if_absent)(void *state, const void *value, bool *inserted);
    void (*close)(void *state);
} cflow_set_state_ops;

typedef struct cflow_sequence_state_ops {
    cflow_status (*open)(void **state, const cmeta_type_desc *type, size_t limit);
    cflow_status (*append)(void *state, const void *value);
    cflow_status (*stable_sort)(void *state);
    cmeta_range (*range)(const void *state);
    void (*close)(void *state);
} cflow_sequence_state_ops;
```

`cflow_eval_options` borrows pointers to these static interfaces. Add explicit graph-add entry points for parameterized nodes; normalization copies the union by value and validates nonzero hard limits.

Use this stream constructor so adapters can inject options without changing the semantic Range:

```c
cflow_stream *cflow_stream_from_range_with_options(
    cflow_stream *stream,
    cmeta_range range,
    const cflow_eval_options *options);
```

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_backend_test cflow_graph_test && ctest --preset win-release-user -R ""^cflow_(backend|graph)_test$"" --output-on-failure"
git add cflow
git commit -m "feat(cflow): add bounded state backend protocols"
```

---

### Task 3: Add finite Java-style intermediate operators

**Files:**
- Modify: `cflow/include/cflow/operators.h`
- Modify: `cflow/include/cflow/meta.h`
- Modify: `cflow/include/cflow/stream.h`
- Modify: `cflow/src/stream.c`
- Modify: `cflow/src/runtime.c`
- Modify: `cflow/src/effect.c`
- Modify: `cflow/src/property.c`
- Create: `cflow/tests/cflow_finite_ops_test.c`
- Modify: `cflow/tests/cflow_test_ops.c`
- Modify: `cflow/tests/cflow_test_ops.h`

**Interfaces:**
- Consumes: Task 2 params/backends and typed callables.
- Produces: `peek`, `limit/take`, `skip`, `takeWhile`, `dropWhile`, `distinct`, `sorted`, and `concat` fluent methods.

- [ ] **Step 1: Write failing observable behavior tests**

Use an instrumented source and assert encounter order, exact pull counts, stable distinct, stable sort, zero/one/exact-limit, mutation-independent graph reuse, and `peek` callback order. Verify `limit(2)` pulls no third downstream value. Add an owning value whose copy/move/destroy counters prove filtered-out, replaced, emitted, cancelled, and failed transient slots are destroyed exactly once.

- [ ] **Step 2: Run RED**

Build `cflow_finite_ops_test`. Expected: methods/operator rows are absent.

- [ ] **Step 3: Extend the operator schema and runtime state machines**

Add rows for parameter/callable categories without encoding runtime state in the schema. Keep state per run:

```c
typedef struct cflow_distinct_state {
    void *backend_state;
    size_t emitted;
} cflow_distinct_state;
```

Open state lazily at run open, close it on DONE/ERROR/cancel, and treat backend full as `CFLOW_CAPACITY_EXCEEDED`. Mark `peek` with the callable's effect and never infer PURE. Implement `concat` as explicit two-source graph topology, not a copied materialized array.

Replace byte-only transient slot assignment with helpers that consume each slot's `cmeta_type_desc`: trivial values use checked byte copy, owning values use copy/move construct, and every overwrite/drop/error/close path calls destroy exactly once. Do not retain pointers returned by a Range across the current step.

- [ ] **Step 4: Run GREEN and adjacent runtime tests**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_finite_ops_test cflow_pipeline_test && ctest --preset win-release-user -R ""^cflow_(finite_ops|pipeline)_test$"" --output-on-failure"
```

- [ ] **Step 5: Commit**

```powershell
git add cflow
git commit -m "feat(cflow): add finite stream operators"
```

---

### Task 4: Add reusable Java-style terminals

**Files:**
- Create: `cflow/include/cflow/terminals.h`
- Create: `cflow/src/terminals.c`
- Modify: `cflow/include/cflow/stream.h`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/src/stream.c`
- Create: `cflow/tests/cflow_terminals_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: CMeta collectors, CFlow eval options, typed match/reduce/consumer callables.
- Produces: separate `CFlowTerminals` schema and terminal methods returning `cflow_status`.

- [ ] **Step 1: Write failing empty, short-circuit, and transaction tests**

```c
it("uses Java empty-stream match semantics") {
    cflow_stream stream = {0};
    bool matched = false;
    check_not_null(cflow_stream_from_range(&stream, empty_int_range()));
    check_equal(stream.anyMatch(&stream, positive, &matched), CFLOW_OK);
    check_false(matched);
    check_equal(stream.allMatch(&stream, positive, &matched), CFLOW_OK);
    check_true(matched);
}
```

Add count, reduce-with/without identity, findFirst/findAny, contains, min/max, forEach, bounded toArray, generic collect success/abort, repeat execution, and callback failure tests.

- [ ] **Step 2: Run RED**

Build `cflow_terminals_test`. Expected: schema/methods missing.

- [ ] **Step 3: Implement one terminal execution template**

`CFlowTerminals` generates method fields/declarations only; `terminals.c` uses a shared executor that opens a fresh source/run, requests demand, records first error, and always closes run/scheduler. Special sinks implement short-circuit behavior. `toArray` preallocates exactly `max_output * aligned_size` after checked arithmetic; it never grows.

Public collect shape:

```c
cflow_status cflow_stream_collect(cflow_stream *stream,
                                  cmeta_collector collector,
                                  size_t max_output);
```

On any error after collector begin, call abort once. On success, finish once. Terminal calls do not mutate graph or bound Range.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_terminals_test cflow_pipeline_test && ctest --preset win-release-user -R ""^cflow_(terminals|pipeline)_test$"" --output-on-failure"
git add cflow
git commit -m "feat(cflow): add reusable stream terminals"
```

---

### Task 5: Protect optimizer and compiled-plan boundaries

**Files:**
- Modify: `cflow/src/lower.c`
- Modify: `cflow/src/opt.c`
- Modify: `cflow/src/plan_compile.c`
- Modify: `cflow/src/plan_exec.c`
- Modify: `cflow/src/verify.c`
- Modify: `cflow/tests/cflow_pipeline_test.c`
- Create: `cflow/tests/cflow_finite_verify_test.c`
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: completed parameterized operators and terminals.
- Produces: normalization/optimization parity, effect barriers, and explicit plan rejection.

- [ ] **Step 1: Write failing parity and rejection tests**

Build pipelines containing limit/skip/takeWhile/dropWhile and effectful peek; compare Surface, normalized, and optimized values/order/status. Assert `cflow_plan_graph_supported()` returns false for distinct/sorted until a compiled implementation exists and compile does not evaluate through runtime.

- [ ] **Step 2: Run RED**

Build `cflow_finite_verify_test`. Expected: validation, parity, or rejection assertions fail.

- [ ] **Step 3: Add exact optimizer/plan admission rules**

Copy params during lowering; treat stateful/effectful nodes as fusion/reorder barriers; extend validation for required callables/limits/backend capabilities. Map only implemented primitives in plan compile and return `CFLOW_UNSUPPORTED` for all others.

- [ ] **Step 4: Run all CFlow tests and Clang syntax checks**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_finite_verify_test cflow_graph_test cflow_pipeline_test cflow_terminals_test && ctest --preset win-release-user -R ""^cflow_"" --output-on-failure"
clang.exe -std=c11 -fsyntax-only -I cmeta/include -I cflow/include -I tinytest/include -I cflow/tests cflow/tests/cflow_terminals_test.c
```

- [ ] **Step 5: Document and commit**

```powershell
rg.exe -n "fallback|placeholder" cflow/README.md cflow/include/cflow
git diff --check -- cflow
git add cflow
git commit -m "feat(cflow): verify finite stream execution boundaries"
```
