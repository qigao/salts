# Standard Container Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `container/` the only standard container library, with trait-aware ownership, namespaced public headers, independent status codes, tests, benchmarks, and migrated Core consumers.

**Architecture:** `TurboUtils::Container` depends only on CMeta. Raw algorithms remain compiled C; CMeta generates header-local typed facades and Range/collector adapters from one schema. Core privately consumes Container but never re-exports its headers.

**Tech Stack:** ISO C11, CMeta traits/Range/collector protocols, TinyTest, CMake Presets, MSVC/Clang, C++17 header smoke tests.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- Execute `2026-08-21-cmeta-container-traits.md` first.
- Container must not include or link Core or CFlow.
- Move only standard containers; buffer/pool/ring/disruptor/bucket-priority-queue stay in Core.
- Public headers live under `<turbo/container/...>`; do not install flat compatibility headers.
- Raw byte initialization is explicitly trivial; owning typed values use CMeta copy/move/destroy.
- Every capacity calculation uses checked arithmetic and every mutation updates generation.
- Existing user changes under `container/` are the implementation baseline; inspect before each edit and never overwrite unrelated work.

---

### Task 1: Establish the independent Container target and public surface

**Files:**
- Create: `container/CMakeLists.txt`
- Create: `container/include/turbo/container/export.h`
- Create: `container/include/turbo/container/status.h`
- Create: `container/include/turbo/container.h`
- Move/rename: `container/include/turbo_*.h` to `container/include/turbo/container/*.h`
- Modify: `CMakeLists.txt`
- Modify: `cmake/TurboUtilsConfig.cmake.in`
- Create: `container/tests/CMakeLists.txt`
- Create: `container/tests/container_header_test.c`
- Create: `container/tests/container_header_cpp_test.cpp`

**Interfaces:**
- Consumes: `TurboUtils::CMeta` and the existing untracked `container/` sources.
- Produces: `turbo_container`, alias/export `TurboUtils::Container`, `container_status`, and namespaced installed headers.

- [ ] **Step 1: Write C and C++ header smoke tests**

```c
#include <turbo/container.h>
#include "tinytest.h"

suite("Container public header") {
    it("exposes raw standard container types") {
        turbo_vec_t vec = {0};
        turbo_hash_map_t map = {0};
        check_true(sizeof(vec) > 0);
        check_true(sizeof(map) > 0);
    }
}
```

The C++ test includes the same aggregate header inside ordinary C++17 code and constructs only zero-initialized raw handles.

- [ ] **Step 2: Run RED after fresh configure**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target container_header_test"
```

Expected: configure or build fails because Container target/public headers are not wired.

- [ ] **Step 3: Create the target and status boundary**

Define:

```c
typedef enum container_status {
    CONTAINER_OK = 0,
    CONTAINER_INVALID_ARGUMENT,
    CONTAINER_OUT_OF_MEMORY,
    CONTAINER_CAPACITY_EXCEEDED,
    CONTAINER_EMPTY,
    CONTAINER_NOT_FOUND,
    CONTAINER_TYPE_MISMATCH,
    CONTAINER_TRAIT_MISSING
} container_status;
```

Configure `turbo_container` with `cmake_config_target(... ALIAS TurboUtils::Container EXPORT_NAME Container)`, public C11, public CMeta dependency, build/install include directories, target export, tests, examples, and benchmarks. Order root subdirectories as `cmeta`, `cflow`, `container`, `utils`, `turbo_serial` so the later optional adapter can resolve CFlow without a cycle.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target container_header_test container_header_cpp_test && ctest --preset win-release-user -R ""^container_header"" --output-on-failure"
git add CMakeLists.txt cmake/TurboUtilsConfig.cmake.in container
git commit -m "build(container): establish standard container target"
```

---

### Task 2: Make Vec, Deque, and Heap trait-aware

**Files:**
- Modify: `container/include/turbo/container/vec.h`
- Modify: `container/include/turbo/container/deque.h`
- Modify: `container/include/turbo/container/heap.h`
- Modify: `container/src/turbo_vec.c`
- Modify: `container/src/turbo_deque.c`
- Modify: `container/src/turbo_heap.c`
- Create: `container/tests/container_sequence_test.c`
- Create: `container/tests/container_ownership_test.c`
- Modify: `container/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: CMeta type descriptors and traits from Plan 1.
- Produces: trait-aware sequence/heap raw APIs, explicit `*_init_bytes`, mutation generation, and stable borrowed-pointer rules.

- [ ] **Step 1: Write failing lifecycle and overflow tests**

Use a counted owning value and assert copy/destroy balance across push, replace, pop, clear, failed reserve, and destroy. Add exact boundary checks:

```c
it("rejects capacity byte overflow without changing the vector") {
    turbo_vec_t vec = {0};
    check_equal(turbo_vec_init_bytes(&vec, sizeof(uint64_t), _Alignof(uint64_t)),
                CONTAINER_OK);
    check_equal(turbo_vec_reserve(&vec, SIZE_MAX), CONTAINER_CAPACITY_EXCEEDED);
    check_size_eq(turbo_vec_size(&vec), 0u);
    turbo_vec_destroy(&vec);
}
```

- [ ] **Step 2: Run RED**

Build `container_sequence_test container_ownership_test`. Expected: new APIs/status semantics are missing.

- [ ] **Step 3: Implement one lifecycle helper path per operation**

Store `const cmeta_type_desc *element_type`, explicit trivial byte size/alignment, and `uint64_t generation` in raw handles. Centralize construct/move/destroy helpers and checked multiplication. Increment generation after successful logical mutation or address-invalidating reserve; never increment on rejected operations.

Keep cleanup single-path:

```c
container_status turbo_vec_push(turbo_vec_t *vec, const void *value) {
    container_status status = turbo_vec_ensure_room(vec, 1u);
    if (status != CONTAINER_OK) return status;
    if (!container_value_copy(vec->type, vec_slot(vec, vec->size), value))
        return CONTAINER_OUT_OF_MEMORY;
    ++vec->size;
    ++vec->generation;
    return CONTAINER_OK;
}
```

- [ ] **Step 4: Run GREEN under MSVC and Clang syntax checks**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target container_sequence_test container_ownership_test && ctest --preset win-release-user -R ""^container_(sequence|ownership)_test$"" --output-on-failure"
clang.exe -std=c11 -fsyntax-only -I cmeta/include -I container/include container/src/turbo_vec.c
```

- [ ] **Step 5: Commit**

```powershell
git add container/include/turbo/container/vec.h container/include/turbo/container/deque.h container/include/turbo/container/heap.h container/src/turbo_vec.c container/src/turbo_deque.c container/src/turbo_heap.c container/tests
git commit -m "feat(container): add trait-aware sequence ownership"
```

---

### Task 3: Make hash containers trait-aware and transactional

**Files:**
- Modify: `container/include/turbo/container/hash_map.h`
- Modify: `container/include/turbo/container/hash_set.h`
- Modify: `container/include/turbo/container/set.h`
- Modify: `container/include/turbo/container/map.h`
- Modify: `container/src/turbo_hash_map.c`
- Modify: `container/src/turbo_set.c`
- Create: `container/tests/container_hash_test.c`

**Interfaces:**
- Consumes: key/value equality, hash, copy, move, and destroy traits.
- Produces: trait-aware HashMap/HashSet/Set/Map with replace-before-destroy semantics and generation tracking.

- [ ] **Step 1: Write failing key/value ownership tests**

Cover missing hash/equality, duplicate insert, value replacement copy failure, remove with and without output, clear, and `capacity + 1`. Assert a failed replacement leaves the old value and generation unchanged.

- [ ] **Step 2: Run RED**

Build `container_hash_test`. Expected: ownership or status assertions fail against byte-copy behavior.

- [ ] **Step 3: Implement key/value transaction boundaries**

Require equality+hash for keys. Construct a replacement value in temporary aligned storage before swapping it into an occupied slot; destroy the old value only after construction succeeds. Rehash by moving live elements and aborting to the original table if allocation fails before migration starts.

- [ ] **Step 4: Run GREEN and adjacent sequence tests**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target container_hash_test container_sequence_test container_ownership_test && ctest --preset win-release-user -R ""^container_(hash|sequence|ownership)_test$"" --output-on-failure"
```

- [ ] **Step 5: Commit**

```powershell
git add container/include/turbo/container/hash_map.h container/include/turbo/container/hash_set.h container/include/turbo/container/set.h container/include/turbo/container/map.h container/src/turbo_hash_map.c container/src/turbo_set.c container/tests/container_hash_test.c container/tests/CMakeLists.txt
git commit -m "feat(container): add trait-aware hash ownership"
```

---

### Task 4: Consolidate typed schemas, trees, adapters, and stable sort

**Files:**
- Modify: `container/include/turbo/container/typed.h`
- Create: `container/include/turbo/container/meta.h`
- Modify: all remaining `container/include/turbo/container/*.h`
- Modify: `container/src/turbo_btree.c`
- Create: `container/src/turbo_sort.c`
- Create: `container/tests/container_typed_test.c`
- Create: `container/tests/container_tree_test.c`
- Create: `container/tests/container_sort_test.c`

**Interfaces:**
- Consumes: raw containers and CMeta Schema/Replay, type traits, Range, collector.
- Produces: sole public `typed/Containers` surface, all standard kinds, mutation-aware ranges, transactional collectors, and `turbo_stable_sort`.

- [ ] **Step 1: Write failing public-DSL tests**

```c
Containers(
    (Vec, IntVec, int),
    (List, IntList, int),
    (HashSet, IntSet, int),
    (HashMap, IntLongMap, int, long),
    (BTree, IntTree, int, long, int_compare)
);

it("generates ranges and collectors from one declaration") {
    IntVec values = {0}, output = {0};
    cmeta_range range;
    check_equal(IntVec_init(&values), CONTAINER_OK);
    check_equal(IntVec_push(&values, 4), CONTAINER_OK);
    check_true(cmeta_container_range_view(&values, CMETA_CONTAINER_VIEW_DEFAULT, &range));
    check_ptr_eq(range.element_type, &cmeta_type_int);
    IntVec_destroy(&values);
}
```

Add compile-negative fixtures for a missing comparator/hash trait and assert the build script reports the intended diagnostic token.

- [ ] **Step 2: Run RED**

Build `container_typed_test container_tree_test container_sort_test`. Expected: schemas, namespaced headers, or stable sort are incomplete.

- [ ] **Step 3: Replay one container-kind schema**

Replace per-header typed macro bodies with one stable schema that maps each kind to raw type, method schema, Range kind, collector kind, and required traits. Keep internal `TURBO_*_DEFINE` helpers private to `meta.h`; install only `typed.h` as the semantic entry.

Implement stable merge sort with checked `count * stride`, caller-supplied type compare trait, a bounded scratch allocation, and trait-aware move/destroy. Generate mutation-aware Range accessors from raw generation functions.

- [ ] **Step 4: Run all Container tests and C++ header smoke**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target container_typed_test container_tree_test container_sort_test container_header_cpp_test && ctest --preset win-release-user -R ""^container_"" --output-on-failure"
```

- [ ] **Step 5: Prove the old macros are not public and commit**

```powershell
rg.exe -n "#define TURBO_.*_DEFINE" container/include/turbo/container -g '!meta.h'
git diff --check -- container
git add container
git commit -m "feat(container): unify typed containers with cmeta"
```

Expected: no public-header definition outside the internal generation header.

---

### Task 5: Migrate Core and TurboSerial consumers, then remove duplicates

**Files:**
- Modify: `utils/CMakeLists.txt`
- Modify: `utils/src/ac_automaton.c`
- Modify: `utils/src/levenshtein_automaton.c`
- Modify: `turbo_serial/CMakeLists.txt`
- Modify: `turbo_serial/turbo_serial.c`
- Modify: `turbo_serial/turbo_serial_internal.h`
- Move: `utils/tests/test_turbo_containers.c` coverage into `container/tests/`
- Move: standard-container sections from `utils/benchmarks/bench_memory_containers.c` into `container/benchmarks/`
- Delete: standard-container headers/sources from `utils/include` and `utils/src`
- Delete: tracked legacy `turbo/` paths already marked for removal
- Modify: `AGENTS.md`

**Interfaces:**
- Consumes: `TurboUtils::Container` and namespaced raw APIs.
- Produces: Core private Container dependency, migrated callers, zero duplicate standard-container files.

- [ ] **Step 1: Add failing dependency/build assertions**

Update Core and TurboSerial sources to include `<turbo/container/vec.h>` while leaving CMake unchanged, then fresh-configure/build `turbo_utils turbo_serial`.

Expected RED: include or link failure proves the target dependency is required rather than obtained through leaked include directories.

- [ ] **Step 2: Wire private dependencies and status conversion**

Link Core and TurboSerial privately to `TurboUtils::Container`. At Core API boundaries map:

```c
static int core_status_from_container(container_status status) {
    switch (status) {
        case CONTAINER_OK: return TURBO_OK;
        case CONTAINER_OUT_OF_MEMORY: return TURBO_ENOMEM;
        case CONTAINER_CAPACITY_EXCEEDED: return TURBO_ERANGE;
        case CONTAINER_INVALID_ARGUMENT: return TURBO_EINVAL;
        default: return TURBO_EIO;
    }
}
```

Keep this mapping at the consuming module boundary; do not add Core headers to Container.

- [ ] **Step 3: Move tests/benchmarks and delete duplicate files**

Preserve every relevant assertion from `test_turbo_containers.c` in the focused Container tests. Move only Vec/Deque/Heap/Hash benchmark cases; leave pool/ring/buffer cases in Core. Update `AGENTS.md` examples to `typed(...)` and new include/target names.

- [ ] **Step 4: Verify consumers and zero stale references**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target turbo_utils turbo_serial test_string_automata container_sequence_test container_hash_test container_tree_test && ctest --preset win-release-user -R ""^(test_string_automata|container_.*|turbo_serial.*)$"" --output-on-failure"
rg.exe -n 'turbo_containers\.h|"turbo_(vec|deque|heap|hash_map|set)\.h"|TURBO_.*_DEFINE' utils turbo_serial AGENTS.md
```

Expected: tests pass; search finds no old includes or public typed macro usage.

- [ ] **Step 5: Commit the completed standard-library migration**

```powershell
git add AGENTS.md CMakeLists.txt utils turbo_serial container cmake/TurboUtilsConfig.cmake.in
git add -A -- turbo
git diff --cached --check
git commit -m "refactor(container): move standard containers out of core"
```
