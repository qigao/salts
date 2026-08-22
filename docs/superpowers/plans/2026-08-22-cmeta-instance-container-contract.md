# CMeta Instance Container Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace TurboSTL's remaining generated-wrapper metadata dependency with instance-level CMeta Range/Collector semantics for all 13 container kinds, including a non-generated associative `cmeta_entry` value with safe borrowed/owned lifetime behavior.

**Architecture:** CMeta owns the generic `cmeta_entry` value and its hash/ordered semantic descriptors. TurboSTL ordinary handles own only kind identity plus bound element/key/value descriptors; each kind exposes Range/Collector factories from the instance, while associative containers expose `KEYS`, `VALUES`, and `ENTRIES` views and use runtime K/V validation at collector accept. Generated `IntVec`/`IntMap` wrapper types remain test-only only until their regression coverage is migrated, then are deleted.

**Tech Stack:** strict C11, C++17 public-header checks, CMeta type descriptors/traits/range/collector APIs, TurboSTL natural instance API, CMake/Ninja, TinyTest, GitHub Actions Linux/Windows release jobs.

**Spec:** `docs/superpowers/specs/2026-08-22-cmeta-instance-container-contract-design.md`

## Global Constraints

- `TurboUtils::STL` depends only on `TurboUtils::CMeta`; CMeta must not depend on TurboSTL.
- All 13 TurboSTL kinds use ordinary runtime handles and declaration DSLs; no new generated typed container or entry type may become public API.
- Associative default view is `ENTRIES`; `KEYS`, `VALUES`, and `ENTRIES` must all be available.
- Entry Range values use one C representation, `cmeta_entry`, with borrowed/owned lifetime semantics.
- `cmeta_type_hash_entry` advertises `COPY | MOVE | DESTROY | EQUAL | HASH`; `cmeta_type_ordered_entry` advertises `COPY | MOVE | DESTROY | COMPARE`.
- A kind may expose an entry descriptor only when its bound K/V descriptors satisfy the concrete requirements behind those advertised traits.
- Entry copy uses K/V `COPY | DESTROY`, allocates correctly aligned storage without Platform, rolls back partial key ownership on value failure, and leaves destination empty on failure.
- Entry move transfers wrapper ownership only; it must never invoke K/V move traits for a borrowed entry and must never mutate the source container.
- Collector accept validates `entry.key_type/value_type` against the output handle on every entry.
- Collector begin validates the semantic entry descriptor and required traits; equivalent descriptors with missing traits must be rejected.
- `destroy()`/collector abort preserve declaration-time kind/type bindings so handles remain reusable.
- Container algorithms, capacity semantics, ordering, mutation generation, and raw-byte APIs are not changed by this plan.
- No GNU `typeof`, no compiler-specific generic inference, and no source-spelling/grep tests.
- CFlow ownership transport is out of scope; explicit `VALUES` views remain the CFlow value-stream path.
- No completion claim until a fresh final-head Linux and Windows CI run passes.

---

### Task 1: Add the CMeta `cmeta_entry` Runtime Value

**Files:**
- Create: `cmeta/include/cmeta/entry.h`
- Create: `cmeta/src/entry.c`
- Create: `cmeta/tests/cmeta_entry_test.c`
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/CMakeLists.txt`
- Modify: `cmeta/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cmeta_type_desc`, `cmeta_type_traits`, `cmeta_type_equal()`, `cmeta_type_require_traits()`.
- Produces:

```c
typedef struct cmeta_entry {
    const cmeta_type_desc *key_type;
    const cmeta_type_desc *value_type;
    const void *key;
    const void *value;
    void *key_storage;
    void *value_storage;
} cmeta_entry;

extern const cmeta_type_desc cmeta_type_hash_entry;
extern const cmeta_type_desc cmeta_type_ordered_entry;
```

The backing `*_storage` pointers are allocation bases owned by the entry. `key`/`value` may point at aligned addresses within those blocks. Borrowed entries have both storage pointers `NULL`.

- [ ] **Step 1: Write a focused failing test**

Create `cmeta/tests/cmeta_entry_test.c` with a tracked owning value type and tests for all four core contracts:

```c
#include <cmeta/entry.h>
#include "tinytest.h"

/* Define tracked_value with COPY/MOVE/DESTROY/EQUAL/HASH/COMPARE callbacks and
 * counters. Use a cmeta_type_desc whose alignment is _Alignof(tracked_value). */

spec("CMeta associative entry") {
    it("copies a borrowed entry into independently owned aligned storage") {
        cmeta_entry borrowed = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key,
            .value = &value
        };
        cmeta_entry owned = {0};

        check_true(cmeta_type_hash_entry.traits->copy_construct(&owned,
                                                                 &borrowed));
        check_not_null(owned.key_storage);
        check_not_null(owned.value_storage);
        check_true(owned.key != borrowed.key);
        check_true(owned.value != borrowed.value);
        check_equal((uintptr_t)owned.key % tracked_type.align, (uintptr_t)0u);
        check_equal((uintptr_t)owned.value % tracked_type.align, (uintptr_t)0u);
        cmeta_type_hash_entry.traits->destroy(&owned);
    }

    it("rolls back copied key when value copy fails") {
        /* Configure tracked copy callback so key succeeds and value fails. */
        check_false(cmeta_type_hash_entry.traits->copy_construct(&owned,
                                                                  &borrowed));
        check_null(owned.key);
        check_null(owned.value);
        check_null(owned.key_storage);
        check_null(owned.value_storage);
        check_equal(live_owned_values, live_before);
    }

    it("moves wrapper ownership without invoking child move callbacks") {
        cmeta_entry borrowed = { ... };
        cmeta_entry moved = {0};
        size_t child_moves_before = tracked_move_calls;
        cmeta_type_ordered_entry.traits->move_construct(&moved, &borrowed);
        check_equal(tracked_move_calls, child_moves_before);
        check_null(borrowed.key);
        check_true(moved.key == &key);
        cmeta_type_ordered_entry.traits->destroy(&moved); /* borrowed: no-op */
    }

    it("advertises only truthful hash and ordered capabilities") {
        check_true((cmeta_type_hash_entry.traits->flags &
                    (CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH)) ==
                   (CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH));
        check_true((cmeta_type_hash_entry.traits->flags & CMETA_TRAIT_COMPARE) == 0u);
        check_true((cmeta_type_ordered_entry.traits->flags & CMETA_TRAIT_COMPARE) != 0u);
        check_true((cmeta_type_ordered_entry.traits->flags &
                    (CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH)) == 0u);
    }
}
```

Also verify `equal/hash/compare` use the key descriptor and reject mismatched key bindings safely.

- [ ] **Step 2: Register the test target and verify RED**

Add:

```cmake
cmake_add_test(cmeta_entry_test
  SOURCES cmeta_entry_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

set_target_properties(cmeta_entry_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

For GNU/Clang, add `cmeta_entry_test` to the existing `-Werror=missing-field-initializers` loop.

Run/CI equivalent:

```bash
cmake --build --preset linux-release-user --target cmeta_entry_test
```

Expected RED: compile failure because `<cmeta/entry.h>` and entry descriptors do not yet exist.

- [ ] **Step 3: Define the public value and descriptor declarations**

Create `cmeta/include/cmeta/entry.h` with the `cmeta_entry` layout and descriptor declarations above. Include it from `cmeta/cmeta.h` after `cmeta_type_desc` and trait APIs are defined, while keeping direct `<cmeta/entry.h>` inclusion valid.

- [ ] **Step 4: Implement portable aligned owned storage**

In `cmeta/src/entry.c`, implement a private allocator that over-allocates with `malloc`, computes an aligned object address using `uintptr_t % align`, stores the allocation base in `key_storage/value_storage`, and checks every `SIZE_MAX` addition before allocation. It must not use Platform, POSIX allocation APIs, or MSVC-specific `_aligned_malloc`.

- [ ] **Step 5: Implement copy/move/destroy**

Copy constructs into a local zero `cmeta_entry temporary`, requires K/V `COPY | DESTROY`, deep-copies key then value, destroys/frees key on value failure, and publishes `temporary` only after both copies succeed. Move copies the wrapper fields and zeroes the source without calling child move callbacks. Destroy invokes child destroy only for owned storage, frees the backing allocations, then zeroes the wrapper.

- [ ] **Step 6: Implement truthful semantic callbacks/descriptors**

Hash-entry equality/hash use the key descriptor only after validating non-null, equal key types and required key traits. Ordered-entry compare uses the key descriptor and `COMPARE`. Define static trait tables with exactly the flags required by the spec and export two `cmeta_type_desc` objects with `sizeof(cmeta_entry)` / `_Alignof(cmeta_entry)`.

- [ ] **Step 7: Add `entry.c` to CMeta and verify GREEN**

Modify `cmeta/CMakeLists.txt`:

```cmake
add_library(${TARGET_NAME}
  src/cmeta.c
  src/entry.c
  src/type_identity.c)
```

Build/run:

```bash
cmake --build --preset linux-release-user --target cmeta_entry_test
ctest --preset linux-release-user -R '^cmeta_entry_test$' --output-on-failure
```

Expected: one focused test target passes with zero failures.

- [ ] **Step 8: Run CMeta regression/header gates**

```bash
ctest --preset linux-release-user -R '^cmeta_' --output-on-failure
```

Expected: all CMeta tests pass; C++17 public-header test still compiles.

- [ ] **Step 9: Commit**

```bash
git add cmeta/include/cmeta/entry.h cmeta/include/cmeta/cmeta.h \
        cmeta/src/entry.c cmeta/CMakeLists.txt \
        cmeta/tests/cmeta_entry_test.c cmeta/tests/CMakeLists.txt
git commit -m "feat(cmeta): add runtime associative entry"
```

---

### Task 2: Give All One-Type TurboSTL Handles Instance CMeta Descriptors

**Files:**
- Modify: `turbostl/include/turbostl/{vec,deque,list,stack,queue,heap,set,hash_set}.h`
- Modify: `turbostl/include/turbostl/typed.h`
- Modify: `turbostl/tests/turbostl_header_typed_test.c`
- Modify: `turbostl/tests/turbostl_header_typed_cpp_test.cpp`
- Add focused runtime assertions to existing sequence/list/hash tests rather than creating spelling tests.

**Interfaces:**
- Consumes: ordinary handles and bound `element_type`; `cmeta_container_desc`, `cmeta_range`, `cmeta_collector`.
- Produces static kind descriptors:

```text
stl_vec_container_desc
stl_deque_container_desc
stl_list_container_desc
stl_stack_container_desc
stl_queue_container_desc
stl_heap_container_desc
stl_set_container_desc
stl_hash_set_container_desc
```

- [ ] **Step 1: Add RED assertions for descriptor identity and reinitialization**

Extend typed header/runtime tests so every one-type DSL declaration satisfies `cmeta_container_descriptor(&handle) != NULL` before `*_init()`, then verify `init -> use -> destroy -> init -> destroy` preserves the same kind descriptor and element binding.

- [ ] **Step 2: Add instance Range tests for each semantic family**

Lock representative flags and mutation behavior: Vec (contiguous/random-access), List (ordered linked), Heap (not sorted), Set (ordered/sorted/unique), HashSet (unique/unordered). Use `cmeta_container_range_view(...DEFAULT...)` only.

- [ ] **Step 3: Add instance collector tests**

For Vec/List/Set representatives, create a DSL-declared zero output handle, obtain its descriptor collector, `begin`, `accept`, `finish`, then verify natural container contents. Abort must destroy runtime storage while preserving descriptor/type binding so the handle can be initialized again.

- [ ] **Step 4: Add/complete kind headers**

Each handle has a `cmeta_container_header` representing its own kind. Stack/Queue gain their own header field in addition to their embedded Vec/Deque raw storage so CMeta erasure does not report Vec/Deque identity.

- [ ] **Step 5: Implement per-kind Range/Collector adapters**

Use existing natural operations and generation counters; do not alter algorithms. The factories read `element_type` from the instance, and return no valid range when the binding is missing. Preserve existing flags from the generated schema.

- [ ] **Step 6: Update declaration DSL initializers**

Every one-type macro binds both kind descriptor and element descriptor without allocation.

- [ ] **Step 7: Verify focused + full TurboSTL tests**

```bash
cmake --build --preset linux-release-user --target turbo_stl
ctest --preset linux-release-user -R '^turbostl_' --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add turbostl/include/turbostl turbostl/tests
git commit -m "feat(turbostl): expose instance metadata for unary containers"
```

---

### Task 3: Add Associative Instance Views and Collectors

**Files:**
- Modify: `turbostl/include/turbostl/{hash_map,map,multimap,btree,bplus_tree}.h`
- Modify: `turbostl/include/turbostl/typed.h`
- Modify/add focused tests in `turbostl/tests/{turbostl_hash_test,turbostl_map_test,turbostl_tree_test,turbostl_entry_test}.c`

**Interfaces:**
- Consumes: `cmeta_entry`, `cmeta_type_hash_entry`, `cmeta_type_ordered_entry`, bound `key_type/value_type`, existing `*_range_next`, slot accessors, and natural `*_put()`.
- Produces five real kind descriptors and complete `DEFAULT/KEYS/VALUES/ENTRIES` views.

- [ ] **Step 1: Write RED tests for view availability and flags**

HashMap must expose keys/values/hash-entry ranges; Map/MultiMap/BTree/BPlusTree must expose ordered key/value/ordered-entry ranges. `DEFAULT` and `ENTRIES` must yield the same semantic entry descriptor and equivalent traversal order.

- [ ] **Step 2: Write RED tests for capability admission**

A hash entry view exists only when key has `EQUAL|HASH|COPY|DESTROY` and value has `COPY|DESTROY`; an ordered entry view exists only when key has `COMPARE|COPY|DESTROY` and value has `COPY|DESTROY`. Missing requirements cause view creation to fail rather than returning a lying descriptor.

- [ ] **Step 3: Implement kind descriptors and Range factories**

Keys/values borrow concrete elements and use the instance descriptors directly. Entry next callbacks write borrowed `cmeta_entry` values with K/V descriptors, K/V pointers, and NULL storage bases. Preserve generation checks through `cmeta_range`.

- [ ] **Step 4: Write RED collector mismatch/trait tests**

Collector begin rejects a semantic entry descriptor lacking its advertised traits. Accept rejects runtime K/V mismatch with `CMETA_TYPE_MISMATCH` and no output mutation.

- [ ] **Step 5: Implement associative collectors**

Begin validates semantic descriptor + required traits and initializes the output. Accept validates `entry.key_type/value_type` against output bindings every time and forwards borrowed pointers to natural `*_put()`. Abort destroys runtime storage while preserving declaration bindings.

- [ ] **Step 6: Verify representative ownership flow**

From an entry range: borrow -> descriptor copy to owned transient -> descriptor move -> collector accept -> descriptor destroy. Verify source container values are never moved/destroyed by wrapper move.

- [ ] **Step 7: Run full TurboSTL tests**

```bash
ctest --preset linux-release-user -R '^turbostl_' --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add turbostl/include/turbostl turbostl/tests
git commit -m "feat(turbostl): add associative instance views"
```

---

### Task 4: Migrate Representative Generated Regressions to Instance Metadata

**Files:**
- Modify: `turbostl/tests/turbostl_list_test.c`
- Modify: `turbostl/tests/turbostl_map_test.c`
- Modify: `turbostl/tests/turbostl_tree_test.c`
- Modify: `turbostl/tests/turbostl_entry_test.c`
- Modify: `turbostl/tests/CMakeLists.txt`

**Interfaces:**
- Consumes only declaration DSLs, natural operations, `cmeta_container_range_view`, `cmeta_entry`, and instance collectors.
- Produces no `RangeIntList`, `OrderedIntMap`, `OwnedEntryMap_entry`, or generated `Type_method` usage in these representative tests.

- [ ] **Step 1: Convert List Range regression**

Replace `typed(List, RangeIntList, int)` with `List(int, list)` inside the test and call `list_init/list_push_back/list_pop_front/list_destroy`. Obtain the range through `cmeta_container_range_view(&list, DEFAULT, &range)` and preserve all mutation/cursor assertions exactly.

- [ ] **Step 2: Convert Map/Set ordering regressions**

Replace generated handles with `Map(int,long,map)` / `Set(int,set)`, pointer-valued natural operations, and instance range views. For Map entries, receive `cmeta_entry` and cast `entry.key/value` to the bound concrete types.

- [ ] **Step 3: Convert BTree/BPlusTree representative ranges**

Use instance keys/values/entries views while preserving sorted/unique flags, capacity limits, mutation invalidation, and order-specific assertions.

- [ ] **Step 4: Convert composed entry ownership regression**

Remove `OwnedEntryMap_entry`/`OwnedEntryTree_entry`. Use borrowed/owned `cmeta_entry` and `cmeta_type_hash_entry` / `cmeta_type_ordered_entry` directly. Preserve the key rollback, move-count, collector trait rejection, and live-object balance assertions.

- [ ] **Step 5: Restore direct test sources in CMake**

Where wrappers such as `legacy_list_test.c`, `legacy_map_test.c`, `legacy_tree_test.c`, or `legacy_typed_test.c` are no longer needed, point the targets back at the real regression source.

- [ ] **Step 6: Verify focused tests and commit**

```bash
ctest --preset linux-release-user -R '^(turbostl_list_test|turbostl_map_test|turbostl_tree_test|turbostl_entry_test)$' --output-on-failure
```

```bash
git add turbostl/tests
git commit -m "test(turbostl): migrate core metadata regressions"
```

---

### Task 5: Remove the Remaining Generated TurboSTL Test Surface

**Files:**
- Modify remaining `turbostl/tests/*.c` and `*.cpp` that consume test-only generated wrappers.
- Delete: `turbostl/tests/legacy_generated_typed.h`
- Delete: `turbostl/tests/legacy_list_test.c`
- Delete: `turbostl/tests/legacy_map_test.c`
- Delete: `turbostl/tests/legacy_tree_test.c`
- Delete: `turbostl/tests/legacy_typed_test.c`
- Modify: `turbostl/tests/CMakeLists.txt`

**Interfaces:**
- Produces a TurboSTL test suite whose typed/container metadata coverage uses only instance handles and natural APIs.

- [ ] **Step 1: Inventory the remaining generated-wrapper tests from build failures/search results**

Do not add a grep test. Use code review/search only to find migration candidates, then prove absence through compilation after deleting the bridge.

- [ ] **Step 2: Convert each remaining regression without weakening assertions**

Preserve Range flags, collector transactionality, duplicate-key behavior, capacity semantics, ownership, iterator invalidation, and generation assertions.

- [ ] **Step 3: Delete the test-only generated bridge and wrappers**

Remove all five legacy files listed above and restore direct CMake source membership.

- [ ] **Step 4: Verify the deletion by compiling all TurboSTL tests**

```bash
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^turbostl_' --output-on-failure
```

Expected: no test needs `CMETA_TYPED_*`, `IntVec`, `IntMap`, or a generated associative entry type.

- [ ] **Step 5: Commit**

```bash
git add -A turbostl/tests
git commit -m "test(turbostl): remove generated container regressions"
```

---

### Task 6: Integration Gate and Return to Natural-API Cleanup

**Files:**
- Modify documentation only if implementation semantics required a spec correction.
- No unrelated production changes.

**Interfaces:**
- Establishes the green checkpoint required before the parent TurboSTL plan resumes removal of temporary `turbo_*` aliases/source names.

- [ ] **Step 1: Run CMeta regressions**

```bash
ctest --preset linux-release-user -R '^cmeta_' --output-on-failure
```

- [ ] **Step 2: Run TurboSTL regressions**

```bash
ctest --preset linux-release-user -R '^turbostl_' --output-on-failure
```

- [ ] **Step 3: Run CFlow integration regressions**

```bash
ctest --preset linux-release-user -R '^cflow_' --output-on-failure
```

- [ ] **Step 4: Run the full repository build/test gate**

```bash
cmake --preset linux-release-user --fresh
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

- [ ] **Step 5: Verify Linux and Windows GitHub Actions on the exact final head**

Both release jobs must complete successfully. Linux must show Configure, Build, and `Test CMeta, CFlow, and TurboSTL` success; Windows must show its combined configure/build/test step success.

- [ ] **Step 6: Review the final public API boundary**

Confirm by public-header/diff review, not a source-spelling test, that TurboSTL metadata no longer depends on generated named container/entry wrappers. Record any remaining temporary `turbo_*` aliases as the next parent-plan task, not as part of this contract.

- [ ] **Step 7: Commit any final documentation-only corrections**

```bash
git add docs/superpowers/specs docs/superpowers/plans
git commit -m "docs(turbostl): record instance metadata contract"
```
