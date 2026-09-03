# Container CFlow Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Container as CFlow's finite-container backend, provide typed Java-style collectors/grouping, remove the legacy Stream runtime, and verify the installed SDK.

**Architecture:** `Salts::CSTLCFlow` is the only bridge. It injects static sequence/set backend strategies into a CFlow stream constructed from mutation-aware CMeta ranges and maps Container collector/status semantics at one boundary.

**Tech Stack:** ISO C11, Salts::CSTL, Salts::CFlow, CMeta adapters, TinyTest, CMake Presets, install-package smoke consumers.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- Execute the CMeta, Container, and CFlow plans first.
- STLCFlow may depend on Container and CFlow; neither dependency may point back to STLCFlow.
- No global registry, hidden backend lookup, unbounded allocation, or fallback to the deleted Stream runtime.
- `stream_keys/values/entries` are mandatory for associative containers; Map has no default stream.
- Outputs are zero-initialized before collect, committed on success, and restored to zero on abort.
- Legacy live/SPSC/window/debounce/C++ Stream APIs are deleted, not reimplemented.

---

### Task 1: Create the STLCFlow target and erased stream adapter

**Files:**
- Create: `cstl/cflow/CMakeLists.txt`
- Create: `cstl/include/cstl/cflow.h`
- Create: `cstl/cflow/cstl_cflow_stream.c`
- Create: `cstl/tests/cstl_cflow_stream_test.c`
- Modify: `cstl/CMakeLists.txt`
- Modify: `cstl/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cmeta_container_desc` Range factories and `cflow_stream_from_range_with_options()`.
- Produces: `salts_stl_cflow`, alias/export `Salts::CSTLCFlow`, and `stream/stream_keys/stream_values/stream_entries` macros.

- [ ] **Step 1: Write failing direct-stream tests**

```c
typed(Vec, IntVec, int);
typed(HashMap, IntLongMap, int, long);

it("binds a typed sequence directly to cflow") {
    IntVec values = {0};
    cflow_stream flow = {0};
    check_equal(IntVec_init(&values), SALTS_STL_OK);
    check_equal(IntVec_push(&values, 1), SALTS_STL_OK);
    check_not_null(stream(&values, &flow));
    check_true(cflow_stream_ok(&flow));
    cflow_stream_destroy(&flow);
    IntVec_destroy(&values);
}
```

Add Map tests proving `stream(&map, ...)` fails and each explicit view succeeds with the correct element descriptor.

- [ ] **Step 2: Run RED**

Fresh configure/build `cstl_cflow_stream_test`. Expected: target/header/functions absent.

- [ ] **Step 3: Implement the erased adapter and target**

Expose underlying functions with macros only for Java-like spelling:

```c
cflow_stream *salts_stl_cflow_stream(void *object,
                                            cmeta_container_view view,
                                            cflow_stream *stream);
#define stream(object, stream_) \
    salts_stl_cflow_stream((object), CMETA_CONTAINER_VIEW_DEFAULT, (stream_))
```

The function reads only the common CMeta container header, resolves the requested Range, injects static Container backend options, and returns NULL with a structured build error on unsupported view/type.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cstl_cflow_stream_test && ctest --preset win-release-user -R ""^cstl_cflow_stream_test$"" --output-on-failure"
git add container
git commit -m "feat(container): bind typed ranges to cflow"
```

---

### Task 2: Implement bounded sequence and set state strategies

**Files:**
- Create: `cstl/cflow/cstl_cflow_backend.c`
- Create: `cstl/cflow/cstl_cflow_internal.h`
- Create: `cstl/tests/cstl_cflow_ops_test.c`
- Modify: `cstl/cflow/CMakeLists.txt`

**Interfaces:**
- Consumes: CFlow sequence/set backend protocols and Container Vec/HashSet/stable sort.
- Produces: static `salts_stl_cflow_eval_options` used by every adapted stream.

- [ ] **Step 1: Write failing distinct/sorted/capacity tests**

Run repeated pipelines over Vec, Deque, List, Set, and tree ranges. Verify stable first-occurrence distinct, stable sorted equal keys, exact limits, limit+1 failure, checked `SIZE_MAX`, and output parity across repeated terminal calls.

- [ ] **Step 2: Run RED**

Build `cstl_cflow_ops_test`. Expected: state operations return `CFLOW_UNSUPPORTED`.

- [ ] **Step 3: Implement adapters with exact ownership mapping**

Sequence `open` initializes a bounded typed Vec; append uses trait-aware copy; stable_sort calls Container sort; range borrows state until close. Set `open` initializes a bounded typed HashSet; insert returns whether the value was first seen. Map every `salts_stl_status` explicitly:

```c
static cflow_status cflow_from_stl(salts_stl_status status) {
    switch (status) {
        case SALTS_STL_OK: return CFLOW_OK;
        case SALTS_STL_INVALID_ARGUMENT: return CFLOW_INVALID_ARGUMENT;
        case SALTS_STL_OUT_OF_MEMORY: return CFLOW_OUT_OF_MEMORY;
        case SALTS_STL_CAPACITY_EXCEEDED: return CFLOW_CAPACITY_EXCEEDED;
        case SALTS_STL_TYPE_MISMATCH: return CFLOW_TYPE_MISMATCH;
        default: return CFLOW_EXECUTION_ERROR;
    }
}
```

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_cflow_ops_test cflow_finite_ops_test && ctest --preset win-release-user -R ""^(cstl_cflow_ops|cflow_finite_ops)_test$"" --output-on-failure"
git add container
git commit -m "feat(container): implement cflow bounded state backends"
```

---

### Task 3: Generate typed sequence/set collectors

**Files:**
- Modify: `cstl/include/cstl/meta.h`
- Modify: `cstl/include/cstl/typed.h`
- Create: `cstl/cflow/cstl_cflow_collectors.c`
- Create: `cstl/tests/cstl_cflow_collect_test.c`

**Interfaces:**
- Consumes: CMeta collector state machine and typed Container descriptors.
- Produces: generated `Name_collector(&out)` factories for Vec/List/Set/HashSet and terminal collect integration.

- [ ] **Step 1: Write failing success/abort/type tests**

Collect mapped `long` values into a typed LongVec, reject collection into IntVec, force capacity+1, inject owning-value copy failure, and assert output is zero with balanced destroy counts. Verify a successful empty collect produces an initialized empty output.

- [ ] **Step 2: Run RED**

Build `cstl_cflow_collect_test`. Expected: collector factories missing.

- [ ] **Step 3: Generate collector factories from the same kind schema**

Each generated collector context stores the zero-initialized output pointer and a private working container. `begin` initializes/reserves the working value; `accept` appends/adds; `finish` moves the working handle into output; `abort` destroys working storage and zeroes output. Do not mutate a pre-existing initialized destination.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_cflow_collect_test cflow_terminals_test && ctest --preset win-release-user -R ""^(cstl_cflow_collect|cflow_terminals)_test$"" --output-on-failure"
git add container
git commit -m "feat(container): generate transactional cflow collectors"
```

---

### Task 4: Add groupingBy and partitioningBy collectors

**Files:**
- Create: `cstl/include/cstl/grouping.h`
- Create: `cstl/cflow/cstl_cflow_grouping.c`
- Modify: `cflow/include/cflow/terminals.h`
- Modify: `cflow/src/terminals.c`
- Create: `cstl/tests/cstl_cflow_grouping_test.c`

**Interfaces:**
- Consumes: typed key mapper/predicate callables, Map/MultiMap collectors, CFlow terminal executor.
- Produces: `groupingBy` and `partitioningBy` with explicit conflict policy and bucket/entry/payload limits.

- [ ] **Step 1: Write failing grouping semantics tests**

Cover empty input, parity grouping, duplicate keys under REJECT/KEEP_FIRST/KEEP_LAST/MERGE, false/true partitions, max bucket count, max total entries, max retained payload, mapper failure, and abort cleanup.

- [ ] **Step 2: Run RED**

Build `cstl_cflow_grouping_test`. Expected: grouping API/terminal support absent.

- [ ] **Step 3: Implement explicit policies and three limits**

```c
typedef enum salts_group_conflict {
    SALTS_GROUP_REJECT,
    SALTS_GROUP_KEEP_FIRST,
    SALTS_GROUP_KEEP_LAST,
    SALTS_GROUP_MERGE
} salts_group_conflict;

typedef struct salts_group_limits {
    size_t max_buckets;
    size_t max_entries;
    size_t max_retained_bytes;
} salts_group_limits;
```

Require a reducer only for MERGE. Use Container Map/MultiMap APIs; update counts only after successful insertion/copy. Overflow or any limit breach aborts the whole output transaction.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_cflow_grouping_test && ctest --preset win-release-user -R ""^cstl_cflow_grouping_test$"" --output-on-failure"
git add cflow/include/cflow/terminals.h cflow/src/terminals.c container
git commit -m "feat(container): add bounded cflow grouping collectors"
```

---

### Task 5: Delete legacy Stream, update docs, and verify the installed SDK

**Files:**
- Delete: tracked `stream/` tree already marked for removal
- Verify deleted: tracked legacy `salts/` tree
- Modify: `CMakeLists.txt`
- Modify: `cmake/SaltsConfig.cmake.in`
- Modify: `cstl/README.md`
- Modify: `cflow/README.md`
- Modify: repository README files that mention old targets/headers
- Create: `cstl/examples/java_stream.c`
- Create: `cstl/tests/install_consumer/CMakeLists.txt`
- Create: `cstl/tests/install_consumer/main.c`

**Interfaces:**
- Consumes: all preceding plans.
- Produces: no legacy Stream/flat-container install surface, complete documentation, example, and install smoke evidence.

- [ ] **Step 1: Migrate the finite legacy behavior matrix**

Use `git show HEAD:stream/tests/...` to enumerate finite container tests, then map each behavior to `cflow_finite_ops_test`, `cflow_terminals_test`, or `cstl_cflow_*_test`. Add missing assertions before accepting deletions. Do not migrate live/SPSC/window/debounce/C++ wrapper tests.

- [ ] **Step 2: Add the installed consumer**

The consumer must use only installed headers/targets:

```cmake
find_package(Salts CONFIG REQUIRED)
add_executable(cstl_stream_consumer main.c)
target_link_libraries(cstl_stream_consumer PRIVATE
  Salts::CSTLCFlow)
```

`main.c` declares `typed(Vec, IntVec, int)`, builds a filter/map/distinct/sorted pipeline, collects to IntVec, validates values, and destroys every owned object.

- [ ] **Step 3: Prove legacy references are absent**

```powershell
rg.exe -n 'Salts::Stream|stream_salts_stls|#include [<"]stream\.h|salts_stls\.h|SALTS_.*_DEFINE' . -g '!build/**' -g '!.git/**' -g '!.worktrees/**' -g '!docs/superpowers/**'
```

Expected: no production, test, example, CMake, or user documentation matches.

- [ ] **Step 4: Run focused, full, sanitizer, and install verification**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure"
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-dev-user && cmake --build --preset win-dev-user --target cmeta_core_test cstl_ownership_test cflow_terminals_test cstl_cflow_grouping_test && ctest --preset win-dev-user -R ""^(cmeta_|cstl_|cflow_)"" --output-on-failure"
cmake --install build/Msvc-Release --prefix build/container-install-smoke
cmake -S cstl/tests/install_consumer -B build/container-consumer -DCMAKE_PREFIX_PATH="$PWD/build/container-install-smoke"
cmake --build build/container-consumer
```

Expected: zero failures, ASan reports no ownership errors, and the consumer builds/runs from the temporary install tree.

- [ ] **Step 5: Run available Clang/GCC validation and benchmark baselines**

First list actual presets, then use the available Windows Clang or Linux GCC user preset. Build all CMeta/Container/CFlow tests. Run Container benchmarks with `benchmark_ops`/`benchmark_bytes`; record observed typical, exact-limit, and saturated cases without claiming unmeasured improvement.

- [ ] **Step 6: Commit and inspect final migration**

```powershell
git add CMakeLists.txt cmake container cflow AGENTS.md
git add -A -- stream container
git diff --cached --check
git status --short
git commit -m "refactor: replace legacy stream with container cflow"
```

Confirm the commit stages only intended legacy deletions, integration, docs, examples, tests, and exports; preserve unrelated user changes.
