# Standard TurboSTL Semantic Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `turbostl/` the only standard container library, with a true linked `List`, red-black-tree ordered containers, open-addressing hash containers, explicit ownership traits, bounded growth, and no semantic aliases.

**Architecture:** `TurboUtils::STL` depends only on CMeta. Compiled C owns list linking, red-black balancing, hash probing, and value lifecycles; CMeta generates typed facades, mutation-aware Range adapters, and collectors. `List`, `Deque`, ordered containers, and hash containers remain distinct algorithm families.

**Tech Stack:** ISO C11, CMeta type traits and Range/collector protocols, TinyTest, CMake Presets, MSVC/Clang, C++17 public-header checks.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- Execute the updated `docs/superpowers/plans/2026-08-21-cmeta-container-traits.md` first; TurboSTL requires CMeta ownership traits, opaque Range cursors, mutation detection, and collectors.
- Treat the current untracked `turbostl/` tree and tracked deletions under `turbo/` as user-owned migration baseline; inspect before every edit and never restore or overwrite unrelated work.
- TurboSTL must not include or link Core or CFlow. It links only `TurboUtils::CMeta`.
- `List` is a doubly linked list and has no `reserve`, `capacity`, or `at(index)` API.
- `Map`, `Set`, and `MultiMap` use the internal red-black tree engine; `HashMap` and `HashSet` use the internal open-addressing hash-table engine.
- `Map` never accepts hash/equal; `HashMap` never promises order; BTree/BPlusTree remain separate algorithms.
- Every `init/from` receives `max_elements`; zero means a valid empty container that cannot grow, never unbounded growth.
- Every successful mutation increments generation exactly once; rejected operations leave contents, size, and generation unchanged.
- Every byte count and aligned offset uses checked arithmetic. Distinguish capacity exceeded from OOM.
- Public headers live under `<turbo/stl/...>`; do not install flat compatibility headers or old `TURBO_*_DEFINE` macros.
- Use TDD for every task: add a behavioral failure, run RED for the expected reason, implement minimal behavior, run GREEN, then refactor.

## File Map

- `turbostl/CMakeLists.txt`: defines/export-installs `TurboUtils::STL`, tests, examples, and benchmarks.
- `turbostl/include/turbo/stl/status.h`: independent stable `turbo_stl_status` values.
- `turbostl/include/turbo/stl/vec.h`, `deque.h`, `list.h`, and `heap.h`: sequence handles and operations.
- `turbostl/include/turbo/stl/map.h`, `set.h`, and `multimap.h`: ordered handles and iterators.
- `turbostl/include/turbo/stl/hash_map.h` and `hash_set.h`: unordered handles and iterators.
- `turbostl/include/turbo/stl/typed.h` and `meta.h`: typed declaration surface and schema replay.
- `turbostl/src/value_internal.h` and `value_internal.c`: checked layout, CMeta lifecycle helpers, and production allocation wrappers.
- `turbostl/src/list.c`: doubly linked list implementation.
- `turbostl/src/rb_tree_internal.h` and `rb_tree_internal.c`: private rotations, fixups, bounds, and invariant validation.
- `turbostl/src/map.c`, `set.c`, and `multimap.c`: adapters over the red-black engine.
- `turbostl/src/hash_table_internal.h` and `hash_table_internal.c`: private probing, tombstones, and rehash.
- `turbostl/src/hash_map.c` and `hash_set.c`: adapters over the hash engine.

---

### Task 1: Establish the target, status, value helpers, and public include surface

**Files:**
- Create: `turbostl/CMakeLists.txt`
- Create: `turbostl/include/turbo/stl/export.h`
- Create: `turbostl/include/turbo/stl/status.h`
- Create: `turbostl/include/turbo/stl.h`
- Create: `turbostl/src/value_internal.h`
- Create: `turbostl/src/value_internal.c`
- Create: `turbostl/tests/CMakeLists.txt`
- Create: `turbostl/tests/expect_compile_failure.cmake`
- Create: `turbostl/tests/turbostl_header_test.c`
- Create: `turbostl/tests/turbostl_header_cpp_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `cmake/TurboUtilsConfig.cmake.in`

**Interfaces:**
- Consumes: `TurboUtils::CMeta`, `cmeta_type_desc`, `cmeta_type_traits`.
- Produces: `TurboUtils::STL`, `turbo_stl_status`, checked size/value helpers, namespaced installed headers.

- [ ] **Step 1: Write C/C++ header RED tests**

```c
#include <turbo/stl.h>
#include "tinytest.h"
suite("TurboSTL public header") {
    it("exposes distinct raw handles") {
        turbo_list_t list = {0}; turbo_deque_t deque = {0};
        turbo_map_t map = {0}; turbo_hash_map_t hash_map = {0};
        check_true(sizeof(list) > 0U && sizeof(deque) > 0U);
        check_true(sizeof(map) > 0U && sizeof(hash_map) > 0U);
    }
}
```

The C++17 test includes the same aggregate header and zero-initializes all four handles.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target turbostl_header_test"
```

Expected: target/header is absent.

- [ ] **Step 3: Define exact status/value interfaces**

```c
typedef enum turbo_stl_status {
    TURBO_STL_OK = 0, TURBO_STL_INVALID_ARGUMENT, TURBO_STL_OUT_OF_MEMORY,
    TURBO_STL_CAPACITY_EXCEEDED, TURBO_STL_EMPTY, TURBO_STL_NOT_FOUND,
    TURBO_STL_TYPE_MISMATCH, TURBO_STL_TRAIT_MISSING
} turbo_stl_status;
turbo_stl_status turbostl_checked_add(size_t, size_t, size_t *);
turbo_stl_status turbostl_checked_mul(size_t, size_t, size_t *);
turbo_stl_status turbostl_checked_align(size_t, size_t, size_t *);
turbo_stl_status turbostl_value_copy(const cmeta_type_desc *, void *, const void *);
turbo_stl_status turbostl_value_move(const cmeta_type_desc *, void *, void *);
void turbostl_value_destroy(const cmeta_type_desc *, void *);
```

Keep production allocation fixed to `malloc/free`; do not add an allocator strategy. Under the test-only `TURBO_STL_TESTING` definition, expose deterministic failure controls from `value_internal.h`:

```c
void turbostl_test_fail_allocation_after(size_t successful_allocations);
void turbostl_test_reset_allocation_failures(void);
```

All implementation allocation sites use private `turbostl_allocate/turbostl_deallocate` wrappers. Production wrappers have no configurable state. Tests compile a private `turbostl_test_objects` object library from the same sources with `TURBO_STL_TESTING`; only that target counts allocations and fails the selected call, and the hooks are never installed/exported. `expect_compile_failure.cmake` invokes the configured compiler for one fixture and fails unless compilation fails with the requested symbol in diagnostics. Missing traits fail before touching storage; COPY failure maps to OOM.

- [ ] **Step 4: Wire target/export and run GREEN**

Use `cmake_config_target(... ALIAS TurboUtils::STL EXPORT_NAME STL)`, public C11/CMeta, namespaced install headers, and root order `cmeta,cflow,turbostl,utils,turbo_serial`.

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target turbostl_header_test turbostl_header_cpp_test && ctest --preset win-release-user -R ""^turbostl_header"" --output-on-failure"
```

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt cmake/TurboUtilsConfig.cmake.in turbostl/CMakeLists.txt turbostl/include/turbo/stl.h turbostl/include/turbo/stl turbostl/src/value_internal.* turbostl/tests
git commit -m "build(turbostl): establish independent container target"
```

---

### Task 2: Migrate Vec, Deque, Stack, Queue, and Heap without changing meanings

**Files:**
- Create: `turbostl/include/turbo/stl/vec.h`
- Create: `turbostl/include/turbo/stl/deque.h`
- Create: `turbostl/include/turbo/stl/stack.h`
- Create: `turbostl/include/turbo/stl/queue.h`
- Create: `turbostl/include/turbo/stl/heap.h`
- Modify: `turbostl/src/turbo_vec.c`
- Modify: `turbostl/src/turbo_deque.c`
- Modify: `turbostl/src/turbo_heap.c`
- Create: `turbostl/tests/turbostl_sequence_test.c`
- Create: `turbostl/tests/turbostl_ownership_test.c`

**Interfaces:**
- Consumes: Task 1 helpers and element traits.
- Produces: bounded trait-aware Vec/Deque/Heap plus Stack/Queue adapters; List is absent.

- [ ] **Step 1: Write bounded-lifecycle RED test**

```c
it("rejects deque max plus one without mutation") {
    turbo_deque_t deque = {0}; int value = 3;
    check_equal(turbo_deque_init(&deque, &cmeta_type_int, 1U), TURBO_STL_OK);
    check_equal(turbo_deque_push_back(&deque, &value), TURBO_STL_OK);
    uint64_t before = turbo_deque_generation(&deque);
    check_equal(turbo_deque_push_front(&deque, &value), TURBO_STL_CAPACITY_EXCEEDED);
    check_equal(turbo_deque_size(&deque), 1U);
    check_equal(turbo_deque_generation(&deque), before);
    turbo_deque_destroy(&deque);
}
```

Add counted owning-value cases for copy failure, pop with/without output, reserve, clear, destroy.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_sequence_test turbostl_ownership_test"
```

Expected: descriptor/max/generation APIs are missing or lifecycle assertions fail.

- [ ] **Step 3: Implement exact initialization contracts**

```c
turbo_stl_status turbo_vec_init(turbo_vec_t *, const cmeta_type_desc *, size_t max_elements);
turbo_stl_status turbo_vec_init_bytes(turbo_vec_t *, size_t size, size_t align, size_t max_elements);
turbo_stl_status turbo_deque_init(turbo_deque_t *, const cmeta_type_desc *, size_t max_elements);
turbo_stl_status turbo_deque_init_bytes(turbo_deque_t *, size_t size, size_t align, size_t max_elements);
```

Reserve before copy; mutate state/generation only after success. Stack exposes push/pop/top over Vec. Queue exposes push/pop/front/back over Deque.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_sequence_test turbostl_ownership_test turbostl_header_cpp_test && ctest --preset win-release-user -R ""^turbostl_(sequence|ownership|header)"" --output-on-failure"
git add turbostl/include/turbo/stl/vec.h turbostl/include/turbo/stl/deque.h turbostl/include/turbo/stl/stack.h turbostl/include/turbo/stl/queue.h turbostl/include/turbo/stl/heap.h turbostl/src/turbo_vec.c turbostl/src/turbo_deque.c turbostl/src/turbo_heap.c turbostl/tests
git commit -m "feat(turbostl): add bounded trait-aware sequences"
```

---

### Task 3: Replace the deque alias with a true doubly linked List

**Files:**
- Create: `turbostl/include/turbo/stl/list.h`
- Create: `turbostl/src/list.c`
- Create: `turbostl/tests/turbostl_list_test.c`
- Create: `turbostl/tests/compile_fail/list_has_no_capacity.c`
- Modify: `turbostl/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 value helpers and element traits.
- Produces: `turbo_list_t`, `turbo_list_iter_t`, stable-node insertion/erasure, linear Range support.

- [ ] **Step 1: Write stable-node and compile-negative RED tests**

```c
it("keeps existing nodes stable across insertion") {
    turbo_list_t list = {0}; turbo_list_iter_t first, second, inserted;
    int one = 1, two = 2, middle = 7;
    check_equal(turbo_list_init(&list, &cmeta_type_int, 3U), TURBO_STL_OK);
    check_equal(turbo_list_push_back(&list, &one, &first), TURBO_STL_OK);
    check_equal(turbo_list_push_back(&list, &two, &second), TURBO_STL_OK);
    check_equal(turbo_list_insert_after(&list, first, &middle, &inserted), TURBO_STL_OK);
    check_equal(*(const int *)turbo_list_iter_value_const(second), 2);
    check_equal(*(const int *)turbo_list_iter_value_const(inserted), 7);
    turbo_list_destroy(&list);
}
```

`list_has_no_capacity.c` calls `turbo_list_capacity`; its driver requires compile failure containing `turbo_list_capacity`.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_list_test turbostl_compile_fail_list"
```

Expected: List is absent/still exposes deque semantics, or the forbidden capacity call compiles.

- [ ] **Step 3: Define the public surface**

```c
typedef struct turbo_list { void *impl; } turbo_list_t;
typedef struct turbo_list_iter { const turbo_list_t *owner; void *node; } turbo_list_iter_t;
turbo_stl_status turbo_list_init(turbo_list_t *, const cmeta_type_desc *, size_t max_elements);
turbo_stl_status turbo_list_push_front(turbo_list_t *, const void *, turbo_list_iter_t *);
turbo_stl_status turbo_list_push_back(turbo_list_t *, const void *, turbo_list_iter_t *);
turbo_stl_status turbo_list_insert_before(turbo_list_t *, turbo_list_iter_t, const void *, turbo_list_iter_t *);
turbo_stl_status turbo_list_insert_after(turbo_list_t *, turbo_list_iter_t, const void *, turbo_list_iter_t *);
turbo_stl_status turbo_list_erase(turbo_list_t *, turbo_list_iter_t, void *out_value);
```

Also declare begin/end/next/prev/value, front/back, pop_front/pop_back, from, clear/destroy, size/empty/generation. Do not declare reserve/capacity/at.

- [ ] **Step 4: Implement checked aligned nodes**

Nodes contain prev/next then aligned payload. Compute allocation with checked align/add, construct before linking, validate owner/node/MOVE before unlinking, and destroy each live payload exactly once on clear/destroy.

- [ ] **Step 5: Add edge/failure cases**

Cover empty, singleton, head/tail/middle operations, reverse iteration, wrong owner, max+1, `SIZE_MAX`, copy failure, move-out, clear reuse, exact destroy count.

- [ ] **Step 6: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_list_test turbostl_ownership_test && ctest --preset win-release-user -R ""^turbostl_(list|ownership)_test$"" --output-on-failure"
git add turbostl/include/turbo/stl/list.h turbostl/src/list.c turbostl/tests
git commit -m "feat(turbostl): implement linked list semantics"
```

---

### Task 4: Isolate open addressing behind HashMap and HashSet

**Files:**
- Create: `turbostl/src/hash_table_internal.h`
- Create: `turbostl/src/hash_table_internal.c`
- Create: `turbostl/include/turbo/stl/hash_map.h`
- Create: `turbostl/include/turbo/stl/hash_set.h`
- Move: `turbostl/src/turbo_hash_map.c` to `turbostl/src/hash_map.c`
- Move: `turbostl/src/turbo_set.c` to `turbostl/src/hash_set.c`
- Create: `turbostl/tests/turbostl_hash_test.c`

**Interfaces:**
- Consumes: Task 1 helpers; key HASH+EQUAL and lifecycle traits.
- Produces: unordered HashMap and key-only HashSet; no ordered Set implementation.

- [ ] **Step 1: Write collision/tombstone RED test**

```c
it("survives all keys colliding and reuses tombstones") {
    turbo_hash_map_t map = {0};
    int keys[] = {1,2,3,4}, values[] = {10,20,30,40};
    check_equal(turbo_hash_map_init_with(&map, &cmeta_type_int, &cmeta_type_int,
                                         constant_hash, int_equal, 4U), TURBO_STL_OK);
    for (size_t i = 0; i < 4U; ++i)
        check_equal(turbo_hash_map_put(&map, &keys[i], &values[i]), TURBO_STL_OK);
    check_equal(turbo_hash_map_remove(&map, &keys[1], NULL), TURBO_STL_OK);
    check_equal(*(const int *)turbo_hash_map_get_const(&map, &keys[3]), 40);
    turbo_hash_map_destroy(&map);
}
```

Add RED cases for replacement-copy failure preserving old value/generation, non-SORTED Range flags, and iterator invalidation after rehash.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_hash_test"
```

Expected: trait APIs or separate HashSet are missing.

- [ ] **Step 3: Extract the private engine**

Store state bytes, hashes, aligned key/value regions, size, slot capacity, tombstones, max elements, generation, descriptors, hash/equal. Keep load <=70% with checked integer comparisons. Empty terminates lookup; tombstone does not.

Define the public customization boundary without exposing slots:

```c
typedef uint64_t (*turbo_hash_fn)(const void *key, void *context);
typedef bool (*turbo_equal_fn)(const void *left, const void *right, void *context);
turbo_stl_status turbo_hash_map_init(turbo_hash_map_t *, const cmeta_type_desc *,
                                     const cmeta_type_desc *, size_t max_elements);
turbo_stl_status turbo_hash_map_init_with(turbo_hash_map_t *, const cmeta_type_desc *,
                                          const cmeta_type_desc *, turbo_hash_fn,
                                          turbo_equal_fn, void *context,
                                          size_t max_elements);
```

`init` consumes HASH/EQUAL traits. `init_with` requires both callbacks and never falls back to byte hashing or `memcmp`. HashSet mirrors these key parameters but has no value descriptor.

- [ ] **Step 4: Make replacement/rehash transactional**

Construct replacement value in aligned temporary storage before destroying old value. Populate a complete next table before swapping; failure preserves original table and generation.

- [ ] **Step 5: Implement key-only HashSet**

Use no value region/dummy value. Duplicate add succeeds without growth. Expose add/contains/remove/iterator/size/capacity/empty/generation.

- [ ] **Step 6: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_hash_test turbostl_ownership_test && ctest --preset win-release-user -R ""^turbostl_(hash|ownership)_test$"" --output-on-failure"
git add turbostl/src/hash_table_internal.h turbostl/src/hash_table_internal.c turbostl/src/hash_map.c turbostl/src/hash_set.c turbostl/include/turbo/stl/hash_map.h turbostl/include/turbo/stl/hash_set.h turbostl/tests
git commit -m "feat(turbostl): isolate hash table containers"
```

---

### Task 5: Implement the private red-black engine and ordered Map/Set

**Files:**
- Create: `turbostl/src/rb_tree_internal.h`
- Create: `turbostl/src/rb_tree_internal.c`
- Create: `turbostl/include/turbo/stl/map.h`
- Create: `turbostl/include/turbo/stl/set.h`
- Create: `turbostl/src/map.c`
- Create: `turbostl/src/set.c`
- Create: `turbostl/tests/turbostl_ordered_test.c`

**Interfaces:**
- Consumes: Task 1 helpers and COMPARE/lifecycle traits.
- Produces: unique-key RB tree, ordered Map/Set, bounds, stable-node iterators, private invariant validator.

- [ ] **Step 1: Write ordered behavior/invariant RED test**

```c
it("iterates map keys in comparator order") {
    turbo_map_t map = {0}; int keys[] = {5,1,3,2,4};
    check_equal(turbo_map_init(&map, &cmeta_type_int, &cmeta_type_int, 5U), TURBO_STL_OK);
    for (size_t i = 0; i < 5U; ++i)
        check_equal(turbo_map_put(&map, &keys[i], &keys[i]), TURBO_STL_OK);
    turbo_map_iter_t it = turbo_map_begin(&map);
    for (int expected = 1; expected <= 5; ++expected) {
        check_equal(*(const int *)turbo_map_iter_key_const(it), expected);
        turbo_map_iter_next(&it);
    }
    check_true(turbo_map_iter_equal(it, turbo_map_end(&map)));
    turbo_map_destroy(&map);
}
```

After every deterministic mutation assert private `turbo_rb_tree_validate(map.impl) == TURBO_STL_OK`.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_ordered_test"
```

Expected: Map aliases HashMap or ordered APIs are absent.

- [ ] **Step 3: Implement nodes/rotation/insertion fixup**

Nodes hold parent/left/right/color and aligned payload. NULL leaves are black. Search uses comparator only. Allocate/copy before linking; insert red; run CLRS fixup; root black; increment size/generation once.

Define comparator-based initialization explicitly:

```c
typedef int (*turbo_compare_fn)(const void *left, const void *right, void *context);
turbo_stl_status turbo_map_init(turbo_map_t *, const cmeta_type_desc *,
                                const cmeta_type_desc *, size_t max_elements);
turbo_stl_status turbo_map_init_with(turbo_map_t *, const cmeta_type_desc *,
                                     const cmeta_type_desc *, turbo_compare_fn,
                                     void *context, size_t max_elements);
```

`init` requires the key COMPARE trait. `init_with` requires an explicit comparator. Neither signature accepts hash/equal. Set mirrors the key/comparator parameters but has no value descriptor.

- [ ] **Step 4: Implement replacement/deletion fixup**

Equal-key put constructs replacement before destroying old value. Delete validates MOVE, transplants without payload copies, restores black height, moves/destroys removed payload, frees one node.

- [ ] **Step 5: Implement iterators/bounds and ordered Set**

Begin=min, end=NULL, next/prev=successor/predecessor, lower_bound=first not-less, upper_bound=first greater. Rotations preserve node identity. Set uses the same engine without values, never HashSet.

- [ ] **Step 6: Expand invariant cases**

Cover all rotation shapes, red/black leaf, one/two-child/root deletion, replacement failure, wrong owner, max+1, and deterministic 2,000-operation trace. Validate root color, red-red, black height, parent links, strict order, exact size.

- [ ] **Step 7: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_ordered_test turbostl_ownership_test && ctest --preset win-release-user -R ""^turbostl_(ordered|ownership)_test$"" --output-on-failure"
git add turbostl/src/rb_tree_internal.h turbostl/src/rb_tree_internal.c turbostl/src/map.c turbostl/src/set.c turbostl/include/turbo/stl/map.h turbostl/include/turbo/stl/set.h turbostl/tests
git commit -m "feat(turbostl): add red black tree map and set"
```

---

### Task 6: Add ordered MultiMap without hidden vectors

**Files:**
- Create: `turbostl/include/turbo/stl/multimap.h`
- Create: `turbostl/src/multimap.c`
- Modify: `turbostl/src/rb_tree_internal.h`
- Modify: `turbostl/src/rb_tree_internal.c`
- Modify: `turbostl/tests/turbostl_ordered_test.c`

**Interfaces:**
- Consumes: Task 5 RB engine.
- Produces: `(key,insertion_sequence)` ordering, equal_range, exact-entry erase, erase-all-by-key.

- [ ] **Step 1: Write duplicate-order RED test**

```c
it("keeps equal multimap keys in insertion order") {
    turbo_multimap_t map = {0}; int key = 3, values[] = {30,31,32};
    check_equal(turbo_multimap_init(&map, &cmeta_type_int, &cmeta_type_int, 3U), TURBO_STL_OK);
    for (size_t i = 0; i < 3U; ++i)
        check_equal(turbo_multimap_put(&map, &key, &values[i], NULL), TURBO_STL_OK);
    turbo_multimap_range_t r = turbo_multimap_equal_range(&map, &key);
    for (size_t i = 0; i < 3U; ++i) {
        check_equal(*(const int *)turbo_multimap_iter_value_const(r.first), values[i]);
        turbo_multimap_iter_next(&r.first);
    }
    turbo_multimap_destroy(&map);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_ordered_test"
```

Expected: MultiMap is absent or still hash-map-to-vector based.

- [ ] **Step 3: Implement composite ordering/removal**

Store monotonic `uint64_t insertion_sequence`; compare key then sequence; reject exhaustion before mutation. `erase(map,iterator,out)` removes one entry; `erase_key(map,key,out_count)` removes full equal range without allocation. Do not provide `remove(key)`.

- [ ] **Step 4: Run GREEN and commit**

Run ordered/ownership tests for equal-range order, head/middle/tail erase, erase-all, missing key, max+1, and lifecycle balance.

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_ordered_test turbostl_ownership_test && ctest --preset win-release-user -R ""^turbostl_(ordered|ownership)_test$"" --output-on-failure"
git add turbostl/include/turbo/stl/multimap.h turbostl/src/multimap.c turbostl/src/rb_tree_internal.h turbostl/src/rb_tree_internal.c turbostl/tests/turbostl_ordered_test.c
git commit -m "feat(turbostl): add ordered multimap semantics"
```

---

### Task 7: Preserve BTree and BPlusTree as separate bounded algorithms

**Files:**
- Create: `turbostl/include/turbo/stl/btree.h`
- Create: `turbostl/include/turbo/stl/bplus_tree.h`
- Move: `turbostl/src/turbo_btree.c` to `turbostl/src/btree.c`
- Create: `turbostl/src/bplus_tree.c`
- Create: `turbostl/tests/turbostl_multiway_tree_test.c`
- Modify: `turbostl/include/turbo/stl.h`
- Modify: `turbostl/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 helpers and COMPARE/lifecycle traits.
- Produces: independently named BTree/BPlusTree APIs with explicit order and hard element limits; neither backs Map/Set.

- [ ] **Step 1: Write separation/boundary RED tests**

Initialize `turbo_btree_t`, `turbo_bplus_tree_t`, and `turbo_map_t` in one translation unit and assert their raw handle types are distinct. For both multiway trees, insert shuffled keys, verify ordered iteration/lookup, reject max+1 without mutation, and balance copy/destroy counts.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_multiway_tree_test"
```

Expected: old Core status/includes, missing namespaced headers, or missing descriptor/max/generation contracts.

- [ ] **Step 3: Migrate without sharing the RB backend**

Keep B-tree node splitting and B+ leaf linking in their own implementation paths. Replace Core status, raw memcpy ownership, unchecked sizing, and flat headers with Task 1 helpers and namespaced APIs. `init(..., max_elements)` uses COMPARE traits; `init_with_order(..., compare, context, min_degree, max_elements)` validates `min_degree >= 2`, checked node fanout bytes, and `max_elements` before allocation. Preserve the algorithms' distinct public type names and do not route any Map/Set call into them.

- [ ] **Step 4: Add failure and invariant coverage**

Cover `max_elements` 0/1/exact/max+1, `SIZE_MAX` arithmetic, split allocation failure at every test hook, replacement-copy failure, root/leaf/internal splits, B+ leaf-chain order, clear reuse, and exact destruction. Validate B-tree key-count/child-count/order invariants after each deterministic mutation.

- [ ] **Step 5: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_multiway_tree_test turbostl_ownership_test turbostl_header_cpp_test && ctest --preset win-release-user -R ""^turbostl_(multiway_tree|ownership|header)_test$"" --output-on-failure"
git add turbostl/include/turbo/stl/btree.h turbostl/include/turbo/stl/bplus_tree.h turbostl/include/turbo/stl.h turbostl/src/btree.c turbostl/src/bplus_tree.c turbostl/tests/turbostl_multiway_tree_test.c turbostl/CMakeLists.txt
git commit -m "refactor(turbostl): preserve bounded multiway trees"
```

---

### Task 8: Generate typed facades, semantic ranges, and collectors from one schema

**Files:**
- Create: `turbostl/include/turbo/stl/meta.h`
- Create: `turbostl/include/turbo/stl/typed.h`
- Modify: `turbostl/include/turbo/stl/*.h`
- Modify: `cmeta/include/cmeta/container.h`
- Create: `turbostl/tests/turbostl_typed_test.c`
- Modify: `turbostl/tests/turbostl_list_test.c`
- Modify: `turbostl/tests/turbostl_ordered_test.c`
- Modify: `turbostl/tests/turbostl_hash_test.c`

**Interfaces:**
- Consumes: Tasks 2-7 raw APIs and prerequisite CMeta cursor/collector protocols.
- Produces: `typed(...)`, `Containers(...)`, per-kind methods, correct Range flags/cursors, transactional collectors.

- [ ] **Step 1: Write typed RED tests**

```c
Containers(
    (List, IntList, int), (Map, IntMap, int, int),
    (Set, IntSet, int), (MultiMap, IntMultiMap, int, int),
    (HashMap, IntHashMap, int, int), (HashSet, IntHashSet, int),
    (BTree, IntBTree, int, int), (BPlusTree, IntBPlusTree, int, int)
);
it("publishes sorted flags only for ordered containers") {
    IntMap ordered = {0}; IntHashMap hashed = {0};
    check_equal(IntMap_init(&ordered, 4U), TURBO_STL_OK);
    check_equal(IntHashMap_init(&hashed, 4U), TURBO_STL_OK);
    check_true((IntMap_keys_range(&ordered).flags & CMETA_RANGE_SORTED) != 0U);
    check_false((IntHashMap_keys_range(&hashed).flags & CMETA_RANGE_SORTED) != 0U);
    IntHashMap_destroy(&hashed); IntMap_destroy(&ordered);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_typed_test turbostl_compile_fail_typed"
```

Expected: schema, `init(max_elements)`, Range adapters, or compile-negative enforcement are absent.

- [ ] **Step 3: Define one kind schema**

Rows record kind, raw type, method family, Range family, collector family, required traits. List uses a node cursor; RB/multiway ordered containers use node+slot successor cursors with SORTED; hash containers use sparse index without SORTED.

- [ ] **Step 4: Prevent semantic leakage**

List generates no capacity/at; Map/Set no reserve/capacity; HashMap/HashSet no bounds; MultiMap only equal_range/erase(iterator)/erase_key; BTree/BPlusTree remain explicitly named and never generate Map aliases.

- [ ] **Step 5: Implement mutation-aware Range adapters**

Capture generation. List/RB store a node in `cmeta_range_cursor.state[0]`; BTree/BPlusTree store the current node in `state[0]` and its slot in `index`, adding parent links internally where successor traversal requires them; Vec/Deque/Hash use `cursor.index`. Return `CMETA_GEN_MUTATED` before dereference on mismatch.

- [ ] **Step 6: Implement transactional collectors**

Begin initializes zero output with hard limit; accept copies; finish commits; abort destroys constructed elements and zeros output. Reject missing traits before begin.

- [ ] **Step 7: Add compile-negative fixtures**

Require diagnostics for Map missing COMPARE, HashMap missing HASH/EQUAL, owning value missing COPY/DESTROY, List capacity, Map reserve.

- [ ] **Step 8: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target turbostl_typed_test turbostl_list_test turbostl_ordered_test turbostl_hash_test turbostl_header_cpp_test && ctest --preset win-release-user -R ""^turbostl_"" --output-on-failure"
git add cmeta/include/cmeta/container.h turbostl/include/turbo/stl turbostl/tests
git commit -m "feat(turbostl): generate semantic typed containers"
```

---

### Task 9: Migrate consumers, remove duplicates, and update docs

**Files:**
- Modify: `utils/CMakeLists.txt`
- Modify: `utils/src/ac_automaton.c`
- Modify: `utils/src/levenshtein_automaton.c`
- Modify: `turbo_serial/CMakeLists.txt`
- Modify: `turbo_serial/turbo_serial.c`
- Modify: `turbo_serial/turbo_serial_internal.h`
- Modify: `turbostl/README.md`
- Modify: `AGENTS.md`
- Move coverage from: `utils/tests/test_turbo_stls.c`
- Move standard benchmarks from: `utils/benchmarks/bench_memory_containers.c`
- Delete migrated standard-container files from: `utils/include`, `utils/src`
- Preserve current tracked deletions under: `turbo/`, `stream/`

**Interfaces:**
- Consumes: Task 8 public API.
- Produces: private Core/TurboSerial dependencies, zero duplicate implementations, corrected examples, migrated coverage.

- [ ] **Step 1: Create dependency RED**

Change one automaton include to `<turbo/stl/vec.h>` without linkage, fresh-configure, build `turbo_utils`; expect include/link failure proving dependency is explicit.

- [ ] **Step 2: Link privately and map status**

```c
static int core_status_from_stl(turbo_stl_status s) {
    switch (s) {
        case TURBO_STL_OK: return TURBO_OK;
        case TURBO_STL_INVALID_ARGUMENT: return TURBO_EINVAL;
        case TURBO_STL_OUT_OF_MEMORY: return TURBO_ENOMEM;
        case TURBO_STL_CAPACITY_EXCEEDED: return TURBO_ERANGE;
        case TURBO_STL_EMPTY: return TURBO_ENOENT;
        case TURBO_STL_NOT_FOUND: return TURBO_ENOENT;
        case TURBO_STL_TYPE_MISMATCH: return TURBO_EINVAL;
        case TURBO_STL_TRAIT_MISSING: return TURBO_EINVAL;
        default: return TURBO_EIO;
    }
}
```

Link Core/TurboSerial privately to TurboSTL; do not re-export includes.

- [ ] **Step 3: Migrate tests/benchmarks by family**

Move Vec/Deque/Heap to sequence tests, HashMap to hash tests, old tree-map to ordered tests, and replace deque-backed List/map-alias assertions. Leave pool/ring/buffer in Core. Benchmark names state the exact container and use `benchmark_ops` with real counts.

- [ ] **Step 4: Rewrite README/examples**

Use `IntList_init(&list,max_elements)` and iterator traversal; never List_at/List_capacity. Show ordered Map separately from unordered HashMap. Document compare vs hash/equal traits.

- [ ] **Step 5: Scan stale references**

```powershell
rg.exe -n 'turbo_stls\.h|"turbo_(vec|deque|list|heap|hash_map|hash_set|map|set|multimap)\.h"|TURBO_.*_DEFINE|deque-backed|map alias' utils turbo_serial container AGENTS.md -g '!build/**'
```

Expected: zero old include, macro, or incorrect semantic phrase.

- [ ] **Step 6: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target turbo_utils turbo_serial test_string_automata turbostl_sequence_test turbostl_list_test turbostl_ordered_test turbostl_hash_test turbostl_multiway_tree_test turbostl_typed_test turbostl_ownership_test && ctest --preset win-release-user -R ""^(test_string_automata|turbostl_.*|turbo_serial.*)$"" --output-on-failure"
git add CMakeLists.txt cmake turbostl utils turbo_serial
git add -A -- turbo stream
git diff --cached --check
git commit -m "refactor(turbostl): enforce standard container semantics"
```

---

### Task 10: Verify sanitizers, install consumption, and measured behavior

**Files:**
- Create: `turbostl/tests/install_consumer/CMakeLists.txt`
- Create: `turbostl/tests/install_consumer/main.c`
- Create: `turbostl/benchmarks/turbostl_benchmark.c`
- Modify: `turbostl/CMakeLists.txt`

**Interfaces:**
- Consumes: completed package.
- Produces: final verification evidence and installed-package consumer.

- [ ] **Step 1: Write install consumer**

Declare typed List/Map/HashMap; initialize finite limits; verify List order, Map sorted iteration, HashMap lookup; destroy all; include only `<turbo/stl.h>` and link only `TurboUtils::STL`.

- [ ] **Step 2: Run Release verification**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure"
```

Record exact pass/fail count and exit code.

- [ ] **Step 3: Run ASan/development verification**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-dev-user && cmake --build --preset win-dev-user --target turbostl_sequence_test turbostl_list_test turbostl_ordered_test turbostl_hash_test turbostl_multiway_tree_test turbostl_typed_test turbostl_ownership_test && ctest --preset win-dev-user -R ""^turbostl_(sequence|list|ordered|hash|multiway_tree|typed|ownership)_test$"" --output-on-failure"
```

- [ ] **Step 4: Verify installed package**

Install to a new workspace temp prefix; configure/build/run consumer with that `CMAKE_PREFIX_PATH`; inspect manifest for namespaced headers only.

- [ ] **Step 5: Run benchmarks**

Measure typical, exact-limit, saturated List push/iterate, Deque push/pop, Map put/get/ordered iterate, HashMap put/get/rehash. Report observations and peak memory; no improvement claim without baseline.

- [ ] **Step 6: Final residue/scope audit**

```powershell
rg.exe -n 'turbo_stls\.h|"turbo_(vec|deque|list|heap|hash_map|hash_set|map|set|multimap)\.h"|TURBO_.*_DEFINE|deque-backed|map alias' . -g '!build/**' -g '!.git/**' -g '!.worktrees/**' -g '!docs/superpowers/**'
git diff --check
git status --short
```

Expected: zero stale production references/whitespace errors; report unrelated dirty paths without modifying them.

- [ ] **Step 7: Commit verification artifacts**

```powershell
git add turbostl/tests/install_consumer turbostl/benchmarks turbostl/CMakeLists.txt
git commit -m "test(turbostl): verify semantic container package"
```
