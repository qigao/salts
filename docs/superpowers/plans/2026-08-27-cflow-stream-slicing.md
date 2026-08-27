# CFlow Stream `take` / `skip` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 CFlow/TurboSTL Stream 提供可复用、保持顺序且能正常短路异步上游的 `take` / `skip` intermediate operations。

**Architecture:** 在 Graph node 中保存不可变 slice 参数，在每个 Run 中保存独立位置计数；runtime 在节点处执行丢弃或短路。Normalization/optimization 保留该元数据，direct compiled plan 本阶段对切片显式 unsupported。

**Tech Stack:** ISO C11、CFlow Graph/Run/Source/Scheduler、CMeta value lifecycle、TurboSTL Stream、TinyTest、CMake Presets。

**Spec:** `docs/superpowers/specs/2026-08-27-cflow-stream-slicing-design.md`

## Global Constraints

- 保持既有 opcode 数值与公开函数签名不变；新增枚举值和 struct 字段只追加。
- Graph 是不可变参数事实源，所有执行位置归单个 Run 所有。
- 所有 Source/managed-value 错误 fail fast，并保留现有首错语义。
- direct plan 对 TAKE/SKIP 返回 unsupported，不做 interpreter fallback。
- Windows 构建与测试只使用版本化 `CMakeUserPresets.json` 的公开 preset。

---

### Task 1: Graph IR 与 Stream fluent API

**Files:**
- Create: `cflow/tests/cflow_stream_slice_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/include/cflow/graph.h`
- Modify: `cflow/include/cflow/stream.h`
- Modify: `cflow/src/graph.c`
- Modify: `cflow/src/stream.c`

**Interfaces:**
- Consumes: `cflow_graph_create_node`、`cflow_graph_connect`、`cflow_stream_init` 的现有 explicit-self 模式。
- Produces: `cflow_graph_create_slice_node`、`cflow_graph_take`、`cflow_graph_skip`、`cflow_stream_take`、`cflow_stream_skip`。

- [x] **Step 1: 写入缺失 API 的编译失败测试**

```c
spec("CFlow Stream slicing") {
    it("composes skip and take in encounter order") {
        const int input[] = {1, 2, 3, 4, 5};
        const int expected[] = {3, 4};
        cflow_stream stream = {0};
        cflow_result result = {0};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.skip(&stream, 2u)->take(&stream, 2u));
        check_true(cflow_eval_array(
            cflow_stream_graph(&stream), input, 5u, &result));
        check_equal(result.count, (size_t)2u);
        check_equal(result.data, expected, sizeof(expected));
        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
    }
}
```

- [x] **Step 2: 配置并构建测试，确认 RED**

Run: `cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cflow_stream_slice_test`

Expected: build fails because `cflow_stream.take`, `cflow_stream.skip`, and their functions do not exist.

- [x] **Step 3: 追加 opcode、slice 参数和构造 API**

```c
typedef struct cflow_slice_parameter {
    bool present;
    size_t count;
} cflow_slice_parameter;

bool cflow_graph_create_slice_node(cflow_graph *graph,
                                   cflow_subgraph_id subgraph,
                                   cflow_op op,
                                   const cmeta_type_desc *input_type,
                                   size_t count,
                                   cflow_node_id *out_node);
bool cflow_graph_take(cflow_graph *graph, size_t limit);
bool cflow_graph_skip(cflow_graph *graph, size_t count);
```

Graph builder must append/connect transactionally, leave the output type unchanged, and reject op values other than TAKE/SKIP.

- [x] **Step 4: 接入 Stream explicit-self 方法**

```c
typedef cflow_stream *(*cflow_stream_slice_method)(cflow_stream *, size_t);

cflow_stream *cflow_stream_take(cflow_stream *stream, size_t limit);
cflow_stream *cflow_stream_skip(cflow_stream *stream, size_t count);
```

Initialize both method pointers in `cflow_stream_init`; failures set the existing Stream/Graph error state.

- [x] **Step 5: 构建测试，确认从编译失败推进到行为失败**

Run: `cmake --build --preset win-release-user --target cflow_stream_slice_test`

Expected: build succeeds and the test fails because runtime has no TAKE/SKIP semantic.

### Task 2: Graph 变换、验证与 interpreted runtime

**Files:**
- Modify: `cflow/src/lower.c`
- Modify: `cflow/src/opt.c`
- Modify: `cflow/src/property.c`
- Modify: `cflow/src/runtime.c`
- Modify: `cflow/src/verify.c`
- Test: `cflow/tests/cflow_stream_slice_test.c`

**Interfaces:**
- Consumes: Task 1 的 slice node 参数与构造 API。
- Produces: 每 Run 独立 slice position、正常短路以及 Surface/normalized/optimized parity。

- [x] **Step 1: 增加边界、重复执行与 plan unsupported 测试**

```c
it("resets slice positions for every evaluation") {
    const int input[] = {1, 2, 3, 4};
    const int expected[] = {2, 3};
    cflow_stream stream = {0};
    cflow_result first = {0}, second = {0};

    check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
    check_not_null(stream.skip(&stream, 1u)->take(&stream, 2u));
    check_true(cflow_eval_array(cflow_stream_graph(&stream), input, 4u, &first));
    check_true(cflow_eval_array(cflow_stream_graph(&stream), input, 4u, &second));
    check_equal(first.data, expected, sizeof(expected));
    check_equal(second.data, expected, sizeof(expected));
    check_false(cflow_plan_graph_supported(cflow_stream_graph(&stream)));
    cflow_result_destroy(&second);
    cflow_result_destroy(&first);
    cflow_stream_destroy(&stream);
}
```

Add literal cases for empty input, `skip(0)`, `skip(size)`, `skip(>size)`, `take(0)`, `take(size)`, `filter -> skip -> take`, and `take -> skip`.

- [x] **Step 2: 运行测试，确认 RED 来自变换或 runtime 缺少 slice semantic**

Run: `ctest --preset win-release-user -R "^cflow_stream_slice_test$" --output-on-failure`

Expected: FAIL with Graph normalization/validation or `operator has no execution semantic`.

- [x] **Step 3: 让 clone/lower/optimize/validate/equality 保留 slice 参数**

Normalization and optimization must call `cflow_graph_create_slice_node`, copy input/output descriptors, reconnect the original data edge, and preserve `present/count`. Structural equality compares both fields. Validation rejects slice nodes with callable, relation, nested graph, mismatched types, or absent parameters; non-slice nodes reject present slice metadata.

- [x] **Step 4: 在 Run 中实现独立位置和短路协议**

```c
typedef struct run_impl {
    /* existing fields */
    size_t *slice_positions;
    bool slice_short_circuit_pending;
    size_t slice_upstream_depth;
    bool zero_take_pending;
} run_impl;
```

`semantic_skip` only increments while position is below the bound and destroys dropped owned values. `semantic_take` increments accepted values and records the current continuation depth when the bound is reached. After the current path settles successfully, cancel Source, mark source done, remove only continuation frames that were upstream at the TAKE node, and retain downstream frames created by the accepted value. `take(0)` performs the same normal short circuit before the first Source resume.

- [x] **Step 5: 运行 slice、runtime 与 pipeline 测试，确认 GREEN**

Run: `ctest --preset win-release-user -R "^(cflow_stream_slice_test|cflow_runtime_test|cflow_pipeline_test)$" --output-on-failure`

Expected: all selected tests pass; `cflow_verify_pipeline` checks interpreted parity and reports `compiled_plan_checked == false` for sliced Graphs.

### Task 3: Source 短路与 managed-value 生命周期

**Files:**
- Modify: `cflow/tests/cflow_stream_slice_test.c`
- Modify: `turbostl/tests/turbostl_managed_stream_test.c`

**Interfaces:**
- Consumes: Task 2 的 normal-short-circuit and slice position behavior。
- Produces: 对无限 Source、错误 Source 与 managed values 的回归证据。

- [x] **Step 1: 添加计数 Source 的失败测试**

```c
typedef struct counting_source {
    size_t resumes;
    size_t cancels;
    int next;
} counting_source;
```

The Source returns one integer VALUE on every resume and records cancel. Assert `take(0)` produces zero values with zero resumes and one cancel; `take(2)` produces `{0, 1}` with two resumes and one cancel; both runs are DONE and not CANCELLED. A separate Source returns ERROR before the limit and must preserve that error.

- [x] **Step 2: 运行测试，确认 RED 能捕获 resume 过量或 CANCELLED 状态**

Run: `ctest --preset win-release-user -R "^cflow_stream_slice_test$" --output-on-failure`

Expected: FAIL if the runtime pulls a third value, reports cancellation, or hides Source error.

- [x] **Step 3: 修正最小 runtime 收尾逻辑并确认 GREEN**

Run: `ctest --preset win-release-user -R "^(cflow_stream_slice_test|cflow_runtime_test|cflow_io_source_test)$" --output-on-failure`

Expected: all selected tests pass with one done callback and no duplicate Source cancel.

- [x] **Step 4: 添加 managed-value 生命周期测试**

Use the existing managed Stream fixture to run `skip(1)->take(1)` over three owned values, collect one value, then destroy output, Stream, input, and originals. Assert the fixture's literal copy/move/destroy counts and selected payload so removing either dropped-value destruction or output ownership makes the test fail.

- [x] **Step 5: 运行 managed suite 与 ASan focused tests**

Run: `ctest --preset win-dev-user -R "^(cflow_stream_slice_test|turbostl_managed_stream_test)$" --output-on-failure`

Expected: both tests pass under AddressSanitizer.

### Task 4: Public headers、TurboSTL 与文档

**Files:**
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `cflow/README.md`
- Modify: `turbostl/README.md`
- Modify: `turbostl/include/turbostl/stream.h`

**Interfaces:**
- Consumes: Task 1-3 的 final public signatures and runtime contract。
- Produces: C++ type check、installed consumer check 与用户文档。

- [x] **Step 1: 添加 C++ signature 与 installed consumer 编译测试**

```cpp
using cflow_stream_slice_function = cflow_stream *(*)(cflow_stream *, size_t);
static_assert(std::is_same<decltype(&cflow_stream_take),
                           cflow_stream_slice_function>::value,
              "Stream take must keep its C signature");
```

The installed consumer initializes a Stream and verifies
`stream.skip(&stream, 1u)->take(&stream, 2u) == &stream` before destruction.

- [x] **Step 2: 构建 C++ header 与 install consumer，确认旧安装不含 API 时失败**

Run: `cmake --build --preset win-release-user --target cflow_header_cpp_test verify_installed_package`

Expected before installation refresh: consumer compilation fails on missing slice API; after Task 4 implementation and install target, it passes.

- [x] **Step 3: 更新 CFlow/TurboSTL 文档与 facade 注释**

Document encounter-order semantics, `take(0)` zero-resume behavior, per-run counters, managed-value support, Source cancellation as normal completion, and direct-plan unsupported status. TurboSTL continues to typedef `cflow_stream`; no duplicate slicing implementation is introduced.

- [x] **Step 4: 运行 header、TurboSTL 与安装验证**

Run: `ctest --preset win-release-user -R "^(cflow_header_cpp_test|turbostl_stream_test|turbostl_stream_cpp_test|turbostl_managed_stream_test)$" --output-on-failure`

Run: `cmake --build --preset install-win-release-user --target install && cmake --build --preset win-release-user --target verify_installed_package`

Expected: all tests and installed-package consumers pass.

### Task 5: 全面验证与提交

**Files:**
- Verify only: all files changed by Tasks 1-4。

**Interfaces:**
- Consumes: complete slice implementation。
- Produces: 可复验的 Release/ASan/impact evidence and one reviewable commit series。

- [x] **Step 1: 同步 CodeGraph 并审查 affected tests**

Run: `codegraph sync . && codegraph affected -p . <changed source and public header files>`

Expected: every reported CFlow/TurboSTL affected test is included in the focused or expanded test runs.

- [x] **Step 2: 运行 Release CFlow/TurboSTL 回归**

Run: `cmake --build --preset win-release-user`

Run: `ctest --preset win-release-user -R "^(cflow_|turbostl_)" --output-on-failure`

Expected: zero failures.

- [x] **Step 3: 运行 focused ASan、完整 Release 与格式检查**

Run: `cmake --fresh --preset win-dev-user && cmake --build --preset win-dev-user --target cflow_stream_slice_test cflow_runtime_test turbostl_managed_stream_test cflow_header_cpp_test`

Run: `ctest --preset win-dev-user -R "^(cflow_stream_slice_test|cflow_runtime_test|turbostl_managed_stream_test|cflow_header_cpp_test)$" --output-on-failure`

Run: `ctest --preset win-release-user --output-on-failure`

Run: `git diff --check`

Expected: all tests pass, ASan reports no error, and `git diff --check` is clean.

- [x] **Step 4: 提交为独立可审查 commits**

```text
test(cflow): specify Stream slicing semantics
feat(cflow): add runtime take and skip operators
docs(cflow): document Stream slicing boundaries
```

Each commit must leave its declared test scope buildable; the final branch must be clean before push.
