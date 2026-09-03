# Standard Container Semantic Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `cstl/` the only standard container library, with a true linked `List`, red-black-tree ordered containers, open-addressing hash containers, explicit ownership traits, bounded growth, and no semantic aliases.

**Architecture:** `Salts::CSTL` depends only on CMeta. Compiled C owns list linking, red-black balancing, hash probing, and value lifecycles; CMeta generates typed facades, mutation-aware Range adapters, and collectors. `List`, `Deque`, ordered containers, and hash containers remain distinct algorithm families.

**Tech Stack:** ISO C11, CMeta type traits and Range/collector protocols, TinyTest, CMake Presets, MSVC/Clang, C++17 public-header checks.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- Execute the updated `docs/superpowers/plans/2026-08-21-cmeta-container-traits.md` first; Container requires CMeta ownership traits, opaque Range cursors, mutation detection, and collectors.
- Treat the current untracked `cstl/` tree and tracked deletions under `salts/` as user-owned migration baseline; inspect before every edit and never restore or overwrite unrelated work.
- Container must not include or link Core or CFlow. It links only `Salts::CMeta`.
- `List` is a doubly linked list and has no `reserve`, `capacity`, or `at(index)` API.
- `Map`, `Set`, and `MultiMap` use the internal red-black tree engine; `HashMap` and `HashSet` use the internal open-addressing hash-table engine.
- `Map` never accepts hash/equal; `HashMap` never promises order; BTree/BPlusTree remain separate algorithms.
- Every `init/from` receives `max_elements`; zero means a valid empty container that cannot grow, never unbounded growth.
- Every successful mutation increments generation exactly once; rejected operations leave contents, size, and generation unchanged.
- Every byte count and aligned offset uses checked arithmetic. Distinguish capacity exceeded from OOM.
- Public headers live under `<cstl/...>`; do not install flat compatibility headers or old `SALTS_*_DEFINE` macros.
- Use TDD for every task: add a behavioral failure, run RED for the expected reason, implement minimal behavior, run GREEN, then refactor.

## File Map

- `cstl/CMakeLists.txt`: defines/export-installs `Salts::CSTL`, tests, examples, and benchmarks.
- `cstl/include/cstl/status.h`: independent stable `salts_stl_status` values.
- `cstl/include/cstl/vec.h`, `deque.h`, `list.h`, and `heap.h`: sequence handles and operations.
- `cstl/include/cstl/map.h`, `set.h`, and `multimap.h`: ordered handles and iterators.
- `cstl/include/cstl/hash_map.h` and `hash_set.h`: unordered handles and iterators.
- `cstl/include/cstl/typed.h` and `meta.h`: typed declaration surface and schema replay.
- `cstl/src/value_internal.h` and `value_internal.c`: checked layout, CMeta lifecycle helpers, and production allocation wrappers.
- `cstl/src/list.c`: doubly linked list implementation.
- `cstl/src/rb_tree_internal.h` and `rb_tree_internal.c`: private rotations, fixups, bounds, and invariant validation.
- `cstl/src/map.c`, `set.c`, and `multimap.c`: adapters over the red-black engine.
- `cstl/src/hash_table_internal.h` and `hash_table_internal.c`: private probing, tombstones, and rehash.
- `cstl/src/hash_map.c` and `hash_set.c`: adapters over the hash engine.

---

### Task 1: Establish the target, status, value helpers, and public include surface

**Files:**
- Create: `cstl/CMakeLists.txt`
- Create: `cstl/include/cstl/status.h`
- Create: `cstl/cstl.h`
- Create: `cstl/src/value_internal.h`
- Create: `cstl/src/value_internal.c`
- Create: `cstl/tests/CMakeLists.txt`
- Create: `cstl/tests/expect_compile_failure.cmake`
- Create: `cstl/tests/cstl_header_test.c`
- Create: `cstl/tests/cstl_header_cpp_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `cmake/SaltsConfig.cmake.in`

**Interfaces:**
- Consumes: `Salts::CMeta`, `cmeta_type_desc`, `cmeta_type_traits`.
- Produces: `Salts::CSTL`, `salts_stl_status`, checked size/value helpers, namespaced installed headers.

- [ ] **Step 1: Write C/C++ header RED tests**

```c
#include <cstl.h>
#include "tinytest.h"
suite("Container public header") {
    it("exposes distinct raw handles") {
        salts_list_t list = {0}; salts_deque_t deque = {0};
        salts_map_t map = {0}; salts_hash_map_t hash_map = {0};
        check_true(sizeof(list) > 0U && sizeof(deque) > 0U);
        check_true(sizeof(map) > 0U && sizeof(hash_map) > 0U);
    }
}
```

The C++17 test includes the same aggregate header and zero-initializes all four handles.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cstl_header_test"
```

Expected: target/header is absent.

- [ ] **Step 3: Define exact status/value interfaces**

```c
typedef enum salts_stl_status {
    SALTS_STL_OK = 0, SALTS_STL_INVALID_ARGUMENT, SALTS_STL_OUT_OF_MEMORY,
    SALTS_STL_CAPACITY_EXCEEDED, SALTS_STL_EMPTY, SALTS_STL_NOT_FOUND,
    SALTS_STL_TYPE_MISMATCH, SALTS_STL_TRAIT_MISSING
} salts_stl_status;
salts_stl_status cstl_checked_add(size_t, size_t, size_t *);
salts_stl_status cstl_checked_mul(size_t, size_t, size_t *);
salts_stl_status cstl_checked_align(size_t, size_t, size_t *);
salts_stl_status cstl_value_copy(const cmeta_type_desc *, void *, const void *);
salts_stl_status cstl_value_move(const cmeta_type_desc *, void *, void *);
void cstl_value_destroy(const cmeta_type_desc *, void *);
```

Keep production allocation fixed to `malloc/free`; do not add an allocator strategy. Under the test-only `SALTS_STL_TESTING` definition, expose deterministic failure controls from `value_internal.h`:

```c
void cstl_test_fail_allocation_after(size_t successful_allocations);
void cstl_test_reset_allocation_failures(void);
```

All implementation allocation sites use private `cstl_allocate/cstl_deallocate` wrappers. Production wrappers have no configurable state. Tests compile a private `cstl_test_objects` object library from the same sources with `SALTS_STL_TESTING`; only that target counts allocations and fails the selected call, and the hooks are never installed/exported. `expect_compile_failure.cmake` invokes the configured compiler for one fixture and fails unless compilation fails with the requested symbol in diagnostics. Missing traits fail before touching storage; COPY failure maps to OOM.

- [ ] **Step 4: Wire target/export and run GREEN**

Use `cmake_config_target(... ALIAS Salts::CSTL EXPORT_NAME STL)`, public C11/CMeta, namespaced install headers, and root order `cmeta,cflow,container,utils,salts_serial`.

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cstl_header_test cstl_header_cpp_test && ctest --preset win-release-user -R ""^cstl_header"" --output-on-failure"
```

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt cmake/SaltsConfig.cmake.in cstl/CMakeLists.txt cstl/cstl.h cstl/include/container cstl/src/value_internal.* cstl/tests
git commit -m "build(container): establish independent container target"
```

---

### Task 2: Migrate Vec, Deque, Stack, Queue, and Heap without changing meanings

**Files:**
- Create: `cstl/include/cstl/vec.h`
- Create: `cstl/include/cstl/deque.h`
- Create: `cstl/include/cstl/stack.h`
- Create: `cstl/include/cstl/queue.h`
- Create: `cstl/include/cstl/heap.h`
- Modify: `cstl/src/salts_vec.c`
- Modify: `cstl/src/salts_deque.c`
- Modify: `cstl/src/salts_heap.c`
- Create: `cstl/tests/cstl_sequence_test.c`
- Create: `cstl/tests/cstl_ownership_test.c`

**Interfaces:**
- Consumes: Task 1 helpers and element traits.
- Produces: bounded trait-aware Vec/Deque/Heap plus Stack/Queue adapters; List is absent.

- [ ] **Step 1: Write bounded-lifecycle RED test**

```c
it("rejects deque max plus one without mutation") {
    salts_deque_t deque = {0}; int value = 3;
    check_equal(salts_deque_init(&deque, &cmeta_type_int, 1U), SALTS_STL_OK);
    check_equal(salts_deque_push_back(&deque, &value), SALTS_STL_OK);
    uint64_t before = salts_deque_generation(&deque);
    check_equal(salts_deque_push_front(&deque, &value), SALTS_STL_CAPACITY_EXCEEDED);
    check_equal(salts_deque_size(&deque), 1U);
    check_equal(salts_deque_generation(&deque), before);
    salts_deque_destroy(&deque);
}
```

Add counted owning-value cases for copy failure, pop with/without output, reserve, clear, destroy.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_sequence_test cstl_ownership_test"
```

Expected: descriptor/max/generation APIs are missing or lifecycle assertions fail.

- [ ] **Step 3: Implement exact initialization contracts**

```c
salts_stl_status salts_vec_init(salts_vec_t *, const cmeta_type_desc *, size_t max_elements);
salts_stl_status salts_vec_init_bytes(salts_vec_t *, size_t size, size_t align, size_t max_elements);
salts_stl_status salts_deque_init(salts_deque_t *, const cmeta_type_desc *, size_t max_elements);
salts_stl_status salts_deque_init_bytes(salts_deque_t *, size_t size, size_t align, size_t max_elements);
```

Reserve before copy; mutate state/generation only after success. Stack exposes push/pop/top over Vec. Queue exposes push/pop/front/back over Deque.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_sequence_test cstl_ownership_test cstl_header_cpp_test && ctest --preset win-release-user -R ""^cstl_(sequence|ownership|header)"" --output-on-failure"
git add cstl/include/cstl/vec.h cstl/include/cstl/deque.h cstl/include/cstl/stack.h cstl/include/cstl/queue.h cstl/include/cstl/heap.h cstl/src/salts_vec.c cstl/src/salts_deque.c cstl/src/salts_heap.c cstl/tests
git commit -m "feat(container): add bounded trait-aware sequences"
```

---

### Task 3: Replace the deque alias with a true doubly linked List

**Files:**
- Create: `cstl/include/cstl/list.h`
- Create: `cstl/src/list.c`
- Create: `cstl/tests/cstl_list_test.c`
- Create: `cstl/tests/compile_fail/list_has_no_capacity.c`
- Modify: `cstl/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 value helpers and element traits.
- Produces: `salts_list_t`, `salts_list_iter_t`, stable-node insertion/erasure, linear Range support.

- [ ] **Step 1: Write stable-node and compile-negative RED tests**

```c
it("keeps existing nodes stable across insertion") {
    salts_list_t list = {0}; salts_list_iter_t first, second, inserted;
    int one = 1, two = 2, middle = 7;
    check_equal(salts_list_init(&list, &cmeta_type_int, 3U), SALTS_STL_OK);
    check_equal(salts_list_push_back(&list, &one, &first), SALTS_STL_OK);
    check_equal(salts_list_push_back(&list, &two, &second), SALTS_STL_OK);
    check_equal(salts_list_insert_after(&list, first, &middle, &inserted), SALTS_STL_OK);
    check_equal(*(const int *)salts_list_iter_value_const(second), 2);
    check_equal(*(const int *)salts_list_iter_value_const(inserted), 7);
    salts_list_destroy(&list);
}
```

`list_has_no_capacity.c` calls `salts_list_capacity`; its driver requires compile failure containing `salts_list_capacity`.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_list_test cstl_compile_fail_list"
```

Expected: List is absent/still exposes deque semantics, or the forbidden capacity call compiles.

- [ ] **Step 3: Define the public surface**

```c
typedef struct salts_list { void *impl; } salts_list_t;
typedef struct salts_list_iter { const salts_list_t *owner; void *node; } salts_list_iter_t;
salts_stl_status salts_list_init(salts_list_t *, const cmeta_type_desc *, size_t max_elements);
salts_stl_status salts_list_push_front(salts_list_t *, const void *, salts_list_iter_t *);
salts_stl_status salts_list_push_back(salts_list_t *, const void *, salts_list_iter_t *);
salts_stl_status salts_list_insert_before(salts_list_t *, salts_list_iter_t, const void *, salts_list_iter_t *);
salts_stl_status salts_list_insert_after(salts_list_t *, salts_list_iter_t, const void *, salts_list_iter_t *);
salts_stl_status salts_list_erase(salts_list_t *, salts_list_iter_t, void *out_value);
```

Also declare begin/end/next/prev/value, front/back, pop_front/pop_back, from, clear/destroy, size/empty/generation. Do not declare reserve/capacity/at.

- [ ] **Step 4: Implement checked aligned nodes**

Nodes contain prev/next then aligned payload. Compute allocation with checked align/add, construct before linking, validate owner/node/MOVE before unlinking, and destroy each live payload exactly once on clear/destroy.

- [ ] **Step 5: Add edge/failure cases**

Cover empty, singleton, head/tail/middle operations, reverse iteration, wrong owner, max+1, `SIZE_MAX`, copy failure, move-out, clear reuse, exact destroy count.

- [ ] **Step 6: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_list_test cstl_ownership_test && ctest --preset win-release-user -R ""^cstl_(list|ownership)_test$"" --output-on-failure"
git add cstl/include/cstl/list.h cstl/src/list.c cstl/tests
git commit -m "feat(container): implement linked list semantics"
```

---

### Task 4: Isolate open addressing behind HashMap and HashSet

**Files:**
- Create: `cstl/src/hash_table_internal.h`
- Create: `cstl/src/hash_table_internal.c`
- Create: `cstl/include/cstl/hash_map.h`
- Create: `cstl/include/cstl/hash_set.h`
- Move: `cstl/src/salts_hash_map.c` to `cstl/src/hash_map.c`
- Move: `cstl/src/salts_set.c` to `cstl/src/hash_set.c`
- Create: `cstl/tests/cstl_hash_test.c`

**Interfaces:**
- Consumes: Task 1 helpers; key HASH+EQUAL and lifecycle traits.
- Produces: unordered HashMap and key-only HashSet; no ordered Set implementation.

- [ ] **Step 1: Write collision/tombstone RED test**

```c
it("survives all keys colliding and reuses tombstones") {
    salts_hash_map_t map = {0};
    int keys[] = {1,2,3,4}, values[] = {10,20,30,40};
    check_equal(salts_hash_map_init_with(&map, &cmeta_type_int, &cmeta_type_int,
                                         constant_hash, int_equal, 4U), SALTS_STL_OK);
    for (size_t i = 0; i < 4U; ++i)
        check_equal(salts_hash_map_put(&map, &keys[i], &values[i]), SALTS_STL_OK);
    check_equal(salts_hash_map_remove(&map, &keys[1], NULL), SALTS_STL_OK);
    check_equal(*(const int *)salts_hash_map_get_const(&map, &keys[3]), 40);
    salts_hash_map_destroy(&map);
}
```

Add RED cases for replacement-copy failure preserving old value/generation, non-SORTED Range flags, and iterator invalidation after rehash.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_hash_test"
```

Expected: trait APIs or separate HashSet are missing.

- [ ] **Step 3: Extract the private engine**

Store state bytes, hashes, aligned key/value regions, size, slot capacity, tombstones, max elements, generation, descriptors, hash/equal. Keep load <=70% with checked integer comparisons. Empty terminates lookup; tombstone does not.

Define the public customization boundary without exposing slots:

```c
typedef uint64_t (*salts_hash_fn)(const void *key, void *context);
typedef bool (*salts_equal_fn)(const void *left, const void *right, void *context);
salts_stl_status salts_hash_map_init(salts_hash_map_t *, const cmeta_type_desc *,
                                     const cmeta_type_desc *, size_t max_elements);
salts_stl_status salts_hash_map_init_with(salts_hash_map_t *, const cmeta_type_desc *,
                                          const cmeta_type_desc *, salts_hash_fn,
                                          salts_equal_fn, void *context,
                                          size_t max_elements);
```

`init` consumes HASH/EQUAL traits. `init_with` requires both callbacks and never falls back to byte hashing or `memcmp`. HashSet mirrors these key parameters but has no value descriptor.

- [ ] **Step 4: Make replacement/rehash transactional**

Construct replacement value in aligned temporary storage before destroying old value. Populate a complete next table before swapping; failure preserves original table and generation.

- [ ] **Step 5: Implement key-only HashSet**

Use no value region/dummy value. Duplicate add succeeds without growth. Expose add/contains/remove/iterator/size/capacity/empty/generation.

- [ ] **Step 6: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_hash_test cstl_ownership_test && ctest --preset win-release-user -R ""^cstl_(hash|ownership)_test$"" --output-on-failure"
git add cstl/src/hash_table_internal.h cstl/src/hash_table_internal.c cstl/src/hash_map.c cstl/src/hash_set.c cstl/include/cstl/hash_map.h cstl/include/cstl/hash_set.h cstl/tests
git commit -m "feat(container): isolate hash table containers"
```

---

### Task 5: Implement the private red-black engine and ordered Map/Set

**Files:**
- Create: `cstl/src/rb_tree_internal.h`
- Create: `cstl/src/rb_tree_internal.c`
- Create: `cstl/include/cstl/map.h`
- Create: `cstl/include/cstl/set.h`
- Create: `cstl/src/map.c`
- Create: `cstl/src/set.c`
- Create: `cstl/tests/cstl_ordered_test.c`

**Interfaces:**
- Consumes: Task 1 helpers and COMPARE/lifecycle traits.
- Produces: unique-key RB tree, ordered Map/Set, bounds, stable-node iterators, private invariant validator.

- [ ] **Step 1: Write ordered behavior/invariant RED test**

```c
it("iterates map keys in comparator order") {
    salts_map_t map = {0}; int keys[] = {5,1,3,2,4};
    check_equal(salts_map_init(&map, &cmeta_type_int, &cmeta_type_int, 5U), SALTS_STL_OK);
    for (size_t i = 0; i < 5U; ++i)
        check_equal(salts_map_put(&map, &keys[i], &keys[i]), SALTS_STL_OK);
    salts_map_iter_t it = salts_map_begin(&map);
    for (int expected = 1; expected <= 5; ++expected) {
        check_equal(*(const int *)salts_map_iter_key_const(it), expected);
        salts_map_iter_next(&it);
    }
    check_true(salts_map_iter_equal(it, salts_map_end(&map)));
    salts_map_destroy(&map);
}
```

After every deterministic mutation assert private `salts_rb_tree_validate(map.impl) == SALTS_STL_OK`.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_ordered_test"
```

Expected: Map aliases HashMap or ordered APIs are absent.

- [ ] **Step 3: Implement nodes/rotation/insertion fixup**

Nodes hold parent/left/right/color and aligned payload. NULL leaves are black. Search uses comparator only. Allocate/copy before linking; insert red; run CLRS fixup; root black; increment size/generation once.

Define comparator-based initialization explicitly:

```c
typedef int (*salts_compare_fn)(const void *left, const void *right, void *context);
salts_stl_status salts_map_init(salts_map_t *, const cmeta_type_desc *,
                                const cmeta_type_desc *, size_t max_elements);
salts_stl_status salts_map_init_with(salts_map_t *, const cmeta_type_desc *,
                                     const cmeta_type_desc *, salts_compare_fn,
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
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_ordered_test cstl_ownership_test && ctest --preset win-release-user -R ""^cstl_(ordered|ownership)_test$"" --output-on-failure"
git add cstl/src/rb_tree_internal.h cstl/src/rb_tree_internal.c cstl/src/map.c cstl/src/set.c cstl/include/cstl/map.h cstl/include/cstl/set.h cstl/tests
git commit -m "feat(container): add red black tree map and set"
```

---

### Task 6: Add ordered MultiMap without hidden vectors

**Files:**
- Create: `cstl/include/cstl/multimap.h`
- Create: `cstl/src/multimap.c`
- Modify: `cstl/src/rb_tree_internal.h`
- Modify: `cstl/src/rb_tree_internal.c`
- Modify: `cstl/tests/cstl_ordered_test.c`

**Interfaces:**
- Consumes: Task 5 RB engine.
- Produces: `(key,insertion_sequence)` ordering, equal_range, exact-entry erase, erase-all-by-key.

- [ ] **Step 1: Write duplicate-order RED test**

```c
it("keeps equal multimap keys in insertion order") {
    salts_multimap_t map = {0}; int key = 3, values[] = {30,31,32};
    check_equal(salts_multimap_init(&map, &cmeta_type_int, &cmeta_type_int, 3U), SALTS_STL_OK);
    for (size_t i = 0; i < 3U; ++i)
        check_equal(salts_multimap_put(&map, &key, &values[i], NULL), SALTS_STL_OK);
    salts_multimap_range_t r = salts_multimap_equal_range(&map, &key);
    for (size_t i = 0; i < 3U; ++i) {
        check_equal(*(const int *)salts_multimap_iter_value_const(r.first), values[i]);
        salts_multimap_iter_next(&r.first);
    }
    salts_multimap_destroy(&map);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_ordered_test"
```

Expected: MultiMap is absent or still hash-map-to-vector based.

- [ ] **Step 3: Implement composite ordering/removal**

Store monotonic `uint64_t insertion_sequence`; compare key then sequence; reject exhaustion before mutation. `erase(map,iterator,out)` removes one entry; `erase_key(map,key,out_count)` removes full equal range without allocation. Do not provide `remove(key)`.

- [ ] **Step 4: Run GREEN and commit**

Run ordered/ownership tests for equal-range order, head/middle/tail erase, erase-all, missing key, max+1, and lifecycle balance.

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_ordered_test cstl_ownership_test && ctest --preset win-release-user -R ""^cstl_(ordered|ownership)_test$"" --output-on-failure"
git add cstl/include/cstl/multimap.h cstl/src/multimap.c cstl/src/rb_tree_internal.h cstl/src/rb_tree_internal.c cstl/tests/cstl_ordered_test.c
git commit -m "feat(container): add ordered multimap semantics"
```

---

### Task 7: Preserve BTree and BPlusTree as separate bounded algorithms

**Files:**
- Create: `cstl/include/cstl/btree.h`
- Create: `cstl/include/cstl/bplus_tree.h`
- Move: `cstl/src/salts_btree.c` to `cstl/src/btree.c`
- Create: `cstl/src/bplus_tree.c`
- Create: `cstl/tests/cstl_multiway_tree_test.c`
- Modify: `cstl/cstl.h`
- Modify: `cstl/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 helpers and COMPARE/lifecycle traits.
- Produces: independently named BTree/BPlusTree APIs with explicit order and hard element limits; neither backs Map/Set.

- [ ] **Step 1: Write separation/boundary RED tests**

Initialize `salts_btree_t`, `salts_bplus_tree_t`, and `salts_map_t` in one translation unit and assert their raw handle types are distinct. For both multiway trees, insert shuffled keys, verify ordered iteration/lookup, reject max+1 without mutation, and balance copy/destroy counts.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_multiway_tree_test"
```

Expected: old Core status/includes, missing namespaced headers, or missing descriptor/max/generation contracts.

- [ ] **Step 3: Migrate without sharing the RB backend**

Keep B-tree node splitting and B+ leaf linking in their own implementation paths. Replace Core status, raw memcpy ownership, unchecked sizing, and flat headers with Task 1 helpers and namespaced APIs. `init(..., max_elements)` uses COMPARE traits; `init_with_order(..., compare, context, min_degree, max_elements)` validates `min_degree >= 2`, checked node fanout bytes, and `max_elements` before allocation. Preserve the algorithms' distinct public type names and do not route any Map/Set call into them.

- [ ] **Step 4: Add failure and invariant coverage**

Cover `max_elements` 0/1/exact/max+1, `SIZE_MAX` arithmetic, split allocation failure at every test hook, replacement-copy failure, root/leaf/internal splits, B+ leaf-chain order, clear reuse, and exact destruction. Validate B-tree key-count/child-count/order invariants after each deterministic mutation.

- [ ] **Step 5: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_multiway_tree_test cstl_ownership_test cstl_header_cpp_test && ctest --preset win-release-user -R ""^cstl_(multiway_tree|ownership|header)_test$"" --output-on-failure"
git add cstl/include/cstl/btree.h cstl/include/cstl/bplus_tree.h cstl/cstl.h cstl/src/btree.c cstl/src/bplus_tree.c cstl/tests/cstl_multiway_tree_test.c cstl/CMakeLists.txt
git commit -m "refactor(container): preserve bounded multiway trees"
```

---

### Task 8: Generate typed facades, semantic ranges, and collectors from one schema

**Files:**
- Create: `cstl/include/cstl/meta.h`
- Create: `cstl/include/cstl/typed.h`
- Modify: `cstl/include/cstl/*.h`
- Modify: `cmeta/include/cmeta/container.h`
- Create: `cstl/tests/cstl_typed_test.c`
- Modify: `cstl/tests/cstl_list_test.c`
- Modify: `cstl/tests/cstl_ordered_test.c`
- Modify: `cstl/tests/cstl_hash_test.c`

**Interfaces:**
- Consumes: Tasks 2-7 raw APIs and prerequisite CMeta cursor/collector protocols.
- Produces: `typed(...)`, per-kind methods, correct Range flags/cursors, transactional collectors.

- [ ] **Step 1: Write typed RED tests**

```c
typed(List, IntList, int);
typed(Map, IntMap, int, int);
typed(Set, IntSet, int);
typed(MultiMap, IntMultiMap, int, int);
typed(HashMap, IntHashMap, int, int);
typed(HashSet, IntHashSet, int);
typed(BTree, IntBTree, int, int);
typed(BPlusTree, IntBPlusTree, int, int);
it("publishes sorted flags only for ordered containers") {
    IntMap ordered = {0}; IntHashMap hashed = {0};
    check_equal(IntMap_init(&ordered, 4U), SALTS_STL_OK);
    check_equal(IntHashMap_init(&hashed, 4U), SALTS_STL_OK);
    check_true((IntMap_keys_range(&ordered).flags & CMETA_RANGE_SORTED) != 0U);
    check_false((IntHashMap_keys_range(&hashed).flags & CMETA_RANGE_SORTED) != 0U);
    IntHashMap_destroy(&hashed); IntMap_destroy(&ordered);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_typed_test cstl_compile_fail_typed"
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
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cstl_typed_test cstl_list_test cstl_ordered_test cstl_hash_test cstl_header_cpp_test && ctest --preset win-release-user -R ""^cstl_"" --output-on-failure"
git add cmeta/include/cmeta/container.h cstl/include/container cstl/tests
git commit -m "feat(container): generate semantic typed containers"
```

---

### Task 9: Migrate consumers, remove duplicates, and update docs

**Files:**
- Modify: `utils/CMakeLists.txt`
- Modify: `utils/src/ac_automaton.c`
- Modify: `utils/src/levenshtein_automaton.c`
- Modify: `salts_serial/CMakeLists.txt`
- Modify: `salts_serial/salts_serial.c`
- Modify: `salts_serial/salts_serial_internal.h`
- Modify: `cstl/README.md`
- Modify: `AGENTS.md`
- Move coverage from: `utils/tests/test_salts_stls.c`
- Move standard benchmarks from: `utils/benchmarks/bench_memory_containers.c`
- Delete migrated standard-container files from: `utils/include`, `utils/src`
- Preserve current tracked deletions under: `salts/`, `stream/`

**Interfaces:**
- Consumes: Task 8 public API.
- Produces: private Core/TurboSerial dependencies, zero duplicate implementations, corrected examples, migrated coverage.

- [ ] **Step 1: Create dependency RED**

Change one automaton include to `<cstl/vec.h>` without linkage, fresh-configure, build `salts`; expect include/link failure proving dependency is explicit.

- [ ] **Step 2: Link privately and map status**

```c
static int core_status_from_stl(salts_stl_status s) {
    switch (s) {
        case SALTS_STL_OK: return SALTS_OK;
        case SALTS_STL_INVALID_ARGUMENT: return SALTS_EINVAL;
        case SALTS_STL_OUT_OF_MEMORY: return SALTS_ENOMEM;
        case SALTS_STL_CAPACITY_EXCEEDED: return SALTS_ERANGE;
        case SALTS_STL_EMPTY: return SALTS_ENOENT;
        case SALTS_STL_NOT_FOUND: return SALTS_ENOENT;
        case SALTS_STL_TYPE_MISMATCH: return SALTS_EINVAL;
        case SALTS_STL_TRAIT_MISSING: return SALTS_EINVAL;
        default: return SALTS_EIO;
    }
}
```

Link Core/TurboSerial privately to Container; do not re-export includes.

- [ ] **Step 3: Migrate tests/benchmarks by family**

Move Vec/Deque/Heap to sequence tests, HashMap to hash tests, old tree-map to ordered tests, and replace deque-backed List/map-alias assertions. Leave pool/ring/buffer in Core. Benchmark names state the exact container and use `benchmark_ops` with real counts.

- [ ] **Step 4: Rewrite README/examples**

Use `IntList_init(&list,max_elements)` and iterator traversal; never List_at/List_capacity. Show ordered Map separately from unordered HashMap. Document compare vs hash/equal traits.

- [ ] **Step 5: Scan stale references**

```powershell
rg.exe -n 'salts_stls\.h|"salts_(vec|deque|list|heap|hash_map|hash_set|map|set|multimap)\.h"|SALTS_.*_DEFINE|deque-backed|map alias' utils salts_serial container AGENTS.md -g '!build/**'
```

Expected: zero old include, macro, or incorrect semantic phrase.

- [ ] **Step 6: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target salts salts_serial test_string_automata cstl_sequence_test cstl_list_test cstl_ordered_test cstl_hash_test cstl_multiway_tree_test cstl_typed_test cstl_ownership_test && ctest --preset win-release-user -R ""^(test_string_automata|cstl_.*|salts_serial.*)$"" --output-on-failure"
git add CMakeLists.txt cmake container utils salts_serial
git add -A -- container stream
git diff --cached --check
git commit -m "refactor(container): enforce standard container semantics"
```

---

### Task 10: Verify sanitizers, install consumption, and measured behavior

**Files:**
- Create: `cstl/tests/install_consumer/CMakeLists.txt`
- Create: `cstl/tests/install_consumer/main.c`
- Create: `cstl/benchmarks/cstl_benchmark.c`
- Modify: `cstl/CMakeLists.txt`

**Interfaces:**
- Consumes: completed package.
- Produces: final verification evidence and installed-package consumer.

- [ ] **Step 1: Write install consumer**

Declare typed List/Map/HashMap; initialize finite limits; verify List order, Map sorted iteration, HashMap lookup; destroy all; include only `<cstl.h>` and link only `Salts::CSTL`.

- [ ] **Step 2: Run Release verification**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure"
```

Record exact pass/fail count and exit code.

- [ ] **Step 3: Run ASan/development verification**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-dev-user && cmake --build --preset win-dev-user --target cstl_sequence_test cstl_list_test cstl_ordered_test cstl_hash_test cstl_multiway_tree_test cstl_typed_test cstl_ownership_test && ctest --preset win-dev-user -R ""^cstl_(sequence|list|ordered|hash|multiway_tree|typed|ownership)_test$"" --output-on-failure"
```

- [ ] **Step 4: Verify installed package**

Install to a new workspace temp prefix; configure/build/run consumer with that `CMAKE_PREFIX_PATH`; inspect manifest for namespaced headers only.

- [ ] **Step 5: Run benchmarks**

Measure typical, exact-limit, saturated List push/iterate, Deque push/pop, Map put/get/ordered iterate, HashMap put/get/rehash. Report observations and peak memory; no improvement claim without baseline.

- [ ] **Step 6: Final residue/scope audit**

```powershell
rg.exe -n 'salts_stls\.h|"salts_(vec|deque|list|heap|hash_map|hash_set|map|set|multimap)\.h"|SALTS_.*_DEFINE|deque-backed|map alias' . -g '!build/**' -g '!.git/**' -g '!.worktrees/**' -g '!docs/superpowers/**'
git diff --check
git status --short
```

Expected: zero stale production references/whitespace errors; report unrelated dirty paths without modifying them.

- [ ] **Step 7: Commit verification artifacts**

```powershell
git add cstl/tests/install_consumer cstl/benchmarks cstl/CMakeLists.txt
git commit -m "test(container): verify semantic container package"
```
