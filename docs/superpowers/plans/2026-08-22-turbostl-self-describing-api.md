# TurboSTL Self-Describing Natural API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace TurboSTL's generated `IntVec`/`IntList`/`IntMap` user model with self-describing `vec_t/list_t/map_t/...` handles declared through `Vec(T,name)` / `Map(K,V,name)` and operated only through natural `vec_*` / `map_*` APIs.

**Architecture:** Each container handle carries its CMeta type binding and starts with `cmeta_container_header`, while one static descriptor per container kind supplies Range/collector factories. Declaration macros bind `CMETA_TYPEOF(T)` once; typed initialization consumes that stored binding. Generated `Type_method` wrappers leave TurboSTL's public path, so CFlow can work with the finite set of ordinary handle kinds rather than every application-defined generated type.

**Tech Stack:** ISO C11, CMeta type/range/collector APIs, CFlow Stream adapter, C++17 header checks, CMake/Ninja, TinyTest, GitHub Actions Linux/Windows.

**Spec:** `docs/superpowers/specs/2026-08-22-turbostl-natural-api-design.md`

**Supersedes:** `docs/superpowers/plans/2026-08-22-turbostl-natural-api.md`

## Global Constraints

- End users never need `IntVec`, `IntList`, `IntMap`, `IntVec_init`, `UserMap_put`, or a generated type token in an operation call.
- Canonical declaration API is `Vec(T,name)`, `Deque(T,name)`, `List(T,name)`, `Stack(T,name)`, `Queue(T,name)`, `Heap(T,name)`, `Set(T,name)`, `HashSet(T,name)`, `Map(K,V,name)`, `HashMap(K,V,name)`, `MultiMap(K,V,name)`, `BTree(K,V,name)`, `BPlusTree(K,V,name)`.
- Canonical operation API is ordinary natural functions such as `vec_init(&v, limit)`, `list_push_back(&l, &value)`, `map_put(&m, &key, &value)`.
- A declaration macro binds metadata only; it performs no allocation.
- Typed `*_init()` reads pre-bound CMeta descriptors and fails without mutation when required descriptors are unresolved.
- `clear()` and `destroy()` preserve type bindings and the CMeta kind descriptor so the same handle can be initialized again.
- Raw-byte APIs remain explicit and separate.
- No GNU `typeof`, compiler-specific type inference, or user-maintained `_Generic` registry.
- Base `TurboUtils::STL` remains dependent only on `TurboUtils::CMeta`; `TurboUtils::STLStream` remains the separate CFlow adapter.
- Preserve all existing container algorithms, ownership behavior, iterator invalidation, capacity semantics, compare/hash semantics, and stable-sort semantics.
- Final installed TurboSTL headers contain no permanent `turbo_*` aliases and no generated TurboSTL typed-wrapper API.
- Do not add grep/source-spelling tests. Verification is compile/link/runtime plus final public-header review.
- No completion claim until a fresh final Linux and Windows CI run succeeds.

---

### Task 1: Establish the Self-Describing Sequence Handle Contract

**Files:**
- Modify: `turbostl/include/turbostl/vec.h`
- Modify: `turbostl/include/turbostl/deque.h`
- Modify: `turbostl/include/turbostl/list.h`
- Modify: `turbostl/include/turbostl/heap.h`
- Modify: `turbostl/include/turbostl/stack.h`
- Modify: `turbostl/include/turbostl/queue.h`
- Replace public role of: `turbostl/include/turbostl/typed.h`
- Modify: `turbostl/include/turbostl.h`
- Modify corresponding implementation files under `turbostl/src/`
- Test: `turbostl/tests/turbostl_sequence_test.c`
- Test: `turbostl/tests/turbostl_list_test.c`
- Test: `turbostl/tests/turbostl_header_test.c`

**Interfaces:**
- Produces ordinary handle types `vec_t`, `deque_t`, `list_t`, `heap_t`, `stack_t`, `queue_t`.
- Produces declaration macros `Vec`, `Deque`, `List`, `Heap`, `Stack`, `Queue`.
- Produces typed init signatures `vec_init(vec_t *, size_t)`, `deque_init(deque_t *, size_t)`, `list_init(list_t *, size_t)`, `heap_init(heap_t *, size_t)`, and corresponding stack/queue wrappers.
- Consumes `CMETA_TYPEOF(T)` from CMeta.

- [ ] **Step 1: Write RED tests for invisible type inference**

Add cases equivalent to:

```c
Vec(int, values);
int input = 7;
int output = 0;

check_equal(vec_init(&values, 8u), STL_OK);
check_equal(vec_push(&values, &input), STL_OK);
check_equal(vec_pop(&values, &output), STL_OK);
check_equal(output, 7);
vec_destroy(&values);
check_equal(vec_init(&values, 16u), STL_OK);
vec_destroy(&values);
```

Add equivalent `List(int, items)`, `Stack(int, stack)`, and `Queue(int, queue)` coverage. The test source must not declare a generated `IntVec` or call any `Type_method` symbol.

- [ ] **Step 2: Run the focused tests and confirm RED**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_sequence_test turbostl_list_test turbostl_header_test
```

Expected: compile failure because current `vec_init/list_init/...` still require explicit descriptors or declaration macros are missing.

- [ ] **Step 3: Put CMeta binding state directly in each sequence handle**

All sequence handles begin with a CMeta header and retain their bound descriptor independently of runtime storage. Conceptually:

```c
typedef struct vec {
    cmeta_container_header cmeta;
    const cmeta_type_desc *element_type;
    void *data;
    size_t size;
    /* existing runtime fields */
} vec_t;
```

`list_t` gains `cmeta` and `element_type` ahead of its runtime `impl/generation` state. `deque_t` and `heap_t` follow the same rule. `stack_t` and `queue_t` keep wrapping their underlying sequence handle.

Do not add a per-element generated C type.

- [ ] **Step 4: Replace TurboSTL typed generation with declaration macros**

`typed.h` no longer registers `CMETA_TYPED_Vec`, `CMETA_TYPED_List`, etc. It becomes the declaration DSL used by the umbrella header:

```c
#define Vec(T, name) \
    vec_t name = { .cmeta = { &stl_vec_container_desc }, \
                   .element_type = CMETA_TYPEOF(T) }
#define List(T, name) \
    list_t name = { .cmeta = { &stl_list_container_desc }, \
                    .element_type = CMETA_TYPEOF(T) }
```

Use equivalent initialization for Deque/Heap and nested `.raw` binding for Stack/Queue.

- [ ] **Step 5: Change typed init/from-array functions to consume the stored binding**

For example:

```c
stl_status vec_init(vec_t *vec, size_t element_limit);
stl_status vec_from_array(vec_t *vec, const void *elements,
                          size_t count, size_t element_limit);
```

`vec_init()` validates `vec != NULL`, `!vec->initialized`, and `vec->element_type != NULL`; it then executes the existing allocation/trait logic using `vec->element_type`.

Apply the same pattern to deque/list/heap. Raw-byte functions retain explicit size/alignment/comparator arguments.

- [ ] **Step 6: Preserve binding across destroy**

Before clearing runtime fields, save `cmeta.descriptor` and the bound type descriptor; restore them after releasing storage. For example:

```c
const cmeta_container_desc *kind = vec->cmeta.descriptor;
const cmeta_type_desc *type = vec->element_type;
/* existing release/reset */
vec->cmeta.descriptor = kind;
vec->element_type = type;
```

Do the equivalent for every self-describing sequence kind.

- [ ] **Step 7: Add unresolved-binding behavior coverage**

Declare an unregistered local type and prove initialization fails without allocation or mutation:

```c
typedef struct Unregistered { int x; } Unregistered;
Vec(Unregistered, unknown);
check_equal(vec_init(&unknown, 4u), STL_INVALID_ARGUMENT);
check_equal(vec_size(&unknown), 0u);
```

- [ ] **Step 8: Run focused sequence tests**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_stl turbostl_sequence_test turbostl_list_test turbostl_header_test
ctest --preset linux-release-user -R "^(turbostl_sequence_test|turbostl_list_test|turbostl_header_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add turbostl/include turbostl/src turbostl/tests
 git commit -m "refactor(turbostl): infer sequence types from handles"
```

---

### Task 2: Establish Self-Describing Associative and Tree Handles

**Files:**
- Modify: `turbostl/include/turbostl/set.h`
- Modify: `turbostl/include/turbostl/hash_set.h`
- Modify: `turbostl/include/turbostl/map.h`
- Modify: `turbostl/include/turbostl/hash_map.h`
- Modify: `turbostl/include/turbostl/multimap.h`
- Modify: `turbostl/include/turbostl/btree.h`
- Modify: `turbostl/include/turbostl/bplus_tree.h`
- Modify corresponding implementation files under `turbostl/src/`
- Modify: `turbostl/include/turbostl/typed.h`
- Test: `turbostl/tests/turbostl_hash_test.c`
- Test: `turbostl/tests/turbostl_map_test.c`
- Test: `turbostl/tests/turbostl_tree_test.c`
- Test: `turbostl/tests/turbostl_ownership_test.c`

**Interfaces:**
- Produces declaration macros `Set`, `HashSet`, `Map`, `HashMap`, `MultiMap`, `BTree`, `BPlusTree`.
- Produces typed init signatures that accept only handle + limit/order configuration, never repeated key/value descriptors.

- [ ] **Step 1: Write RED user-facing map/set tests**

Use only:

```c
Map(int, int, values);
int key = 3;
int value = 9;
check_equal(map_init(&values, 16u), STL_OK);
check_equal(map_put(&values, &key, &value), STL_OK);
check_equal(*(const int *)map_get_const(&values, &key), 9);
map_destroy(&values);
check_equal(map_init(&values, 32u), STL_OK);
map_destroy(&values);
```

Add equivalent Set/HashSet/HashMap/BTree smoke coverage.

- [ ] **Step 2: Run focused tests and confirm RED**

Run the map/hash/tree test targets. Expected: compile failures where descriptors are still required explicitly or natural names are incomplete.

- [ ] **Step 3: Add persistent bindings to public handles**

One-type associative handles carry `element_type`; two-type handles carry `key_type` and `value_type`, plus `cmeta_container_header` as their first field.

For example:

```c
typedef struct map {
    cmeta_container_header cmeta;
    const cmeta_type_desc *key_type;
    const cmeta_type_desc *value_type;
    void *impl;
    uint64_t generation;
} map_t;
```

Do not store the binding only inside `impl`, because it must exist before allocation and survive destroy.

- [ ] **Step 4: Change typed initialization to consume the bound descriptors**

For example:

```c
stl_status map_init(map_t *map, size_t entry_limit);
stl_status hash_map_init(hash_map_t *map, size_t entry_limit);
stl_status set_init(set_t *set, size_t element_limit);
```

BTree/BPlusTree keep explicit order/fanout arguments only where those are real algorithm configuration; key/value descriptors come from the handle.

- [ ] **Step 5: Preserve bindings across destroy and reject unresolved bindings**

Use the same save/reset/restore rule from Task 1. Ordered containers must additionally require compare traits; hash containers must continue requiring hash/equality traits. Do not weaken trait validation.

- [ ] **Step 6: Run focused associative/tree tests**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_hash_test turbostl_map_test turbostl_tree_test turbostl_ownership_test
ctest --preset linux-release-user -R "^(turbostl_hash_test|turbostl_map_test|turbostl_tree_test|turbostl_ownership_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add turbostl/include turbostl/src turbostl/tests
 git commit -m "refactor(turbostl): infer associative types from handles"
```

---

### Task 3: Replace Generated TurboSTL Typed Wrappers with Kind-Level CMeta Descriptors

**Files:**
- Modify: `turbostl/include/turbostl/meta.h`
- Modify: `turbostl/include/turbostl/typed.h`
- Modify: `turbostl/include/turbostl.h`
- Modify: `cmeta/include/cmeta/container.h` only if a generic helper is needed without changing CMeta's public typed-generation contract
- Create or modify TurboSTL kind descriptor definitions under `turbostl/src/`
- Test: `turbostl/tests/turbostl_entry_test.c`
- Test: `turbostl/tests/turbostl_header_typed_test.c`
- Test: `turbostl/tests/turbostl_header_typed_cpp_test.cpp`

**Interfaces:**
- Produces static descriptors such as `stl_vec_container_desc`, `stl_list_container_desc`, `stl_map_container_desc`.
- Removes TurboSTL public reliance on `CMETA_CONTAINER1_DEFINE(IntVec, ...)` and generated `Type_method` symbols.
- Keeps CMeta's generic facilities available for other users.

- [ ] **Step 1: Rewrite typed-header tests around declaration macros**

A typed header smoke test should look like:

```c
#include <turbostl/typed.h>

Vec(int, values);
Map(int, int, scores);

check_equal(vec_init(&values, 4u), STL_OK);
check_equal(map_init(&scores, 4u), STL_OK);
```

There is no `typed(Vec, IntVec, int)` declaration.

- [ ] **Step 2: Define one static CMeta container descriptor per kind**

Each handle's `cmeta.descriptor` points to a kind descriptor. The descriptor's range factories read actual element/key/value descriptors from the object, so the static descriptor does not encode a specific `int` instantiation.

- [ ] **Step 3: Remove TurboSTL generic-kind registrations and generated wrapper macros from public typed.h/meta.h**

Delete the public path that emits `CMETA_TYPED_Vec`, `CMETA_TYPED_List`, `CMETA_TYPED_Map`, `STL_*_DEFINE`, or generated `Name_method` user APIs where they are only supporting the old model.

Keep shared non-wrapper helpers only when they are still required by direct Range/collector adapters.

- [ ] **Step 4: Build C/C++ public-header tests**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_header_test turbostl_header_cpp_test turbostl_header_typed_test turbostl_header_typed_cpp_test
```

Expected: PASS without generated type declarations.

- [ ] **Step 5: Commit**

```bash
git add turbostl/include turbostl/src turbostl/tests cmeta/include/cmeta/container.h
 git commit -m "refactor(turbostl): remove generated user container types"
```

---

### Task 4: Make Range and Collector Integration Instance-Driven

**Files:**
- Modify: `turbostl/include/turbostl/meta.h` or split focused internal adapter declarations if it becomes too large
- Modify/create TurboSTL Range/collector implementation files
- Modify: `turbostl/include/turbostl/stream.h`
- Test: `turbostl/tests/turbostl_entry_test.c`
- Test: `turbostl/tests/turbostl_stream_test.c` if present

**Interfaces:**
- Produces direct `cmeta_range` factories for ordinary handles.
- Produces collectors bound to an output handle, not to a generated type token.

- [ ] **Step 1: Add direct Range tests**

After initializing `Vec(int, values)`, obtain its CMeta range through the object's `cmeta_container_header` and verify size/iteration/version mutation behavior matches the previous typed wrapper behavior.

- [ ] **Step 2: Implement kind-level range factories**

For a vector, the factory reads `vec->element_type`, `vec_size`, `vec_at_const`, and `vec_generation`. Linked/tree factories use the existing range-next helpers and the bound descriptor in the handle.

Map descriptors provide default/keys/values/entries views using `map->key_type` / `map->value_type` and existing iterators.

- [ ] **Step 3: Implement output-handle collectors**

Collector factories receive the zero-state output handle, verify the incoming type against its pre-bound descriptor, call natural `*_init(output, limit)`, copy accepted values through natural APIs, and destroy/abort while preserving binding.

- [ ] **Step 4: Run Range/collector tests**

Expected: iteration, mutation detection, collector commit, capacity failure, and abort ownership semantics all pass without generated types.

- [ ] **Step 5: Commit**

```bash
git add turbostl/include turbostl/src turbostl/tests
 git commit -m "refactor(turbostl): bind CMeta adapters to container instances"
```

---

### Task 5: Simplify CFlow Stream/Terminal API Around Ordinary Handles

**Files:**
- Modify: `turbostl/include/turbostl/stream.h`
- Modify TurboSTL/CFlow integration tests/examples
- Modify CFlow only if required to accept the same `cmeta_container_header` contract; do not change CFlow execution semantics

**Interfaces:**
- Consumes existing `cflow_stream_from_object()` and `cflow_eval_collect()`.
- Produces terminal usage `to_list(&pipeline, &output, limit)` without an output type token.

- [ ] **Step 1: Write RED stream usage**

Use:

```c
Vec(int, input);
List(int, output);
cflow_stream pipeline = {0};

vec_init(&input, 16u);
stream(&input, &pipeline);
/* operators */
result = to_list(&pipeline, &output, 16u);
```

No `InputVec`, `OutputList`, or generated collector symbol appears.

- [ ] **Step 2: Reuse cflow_stream_from_object directly**

Because every ordinary handle begins with a valid `cmeta_container_header`, existing `stream(object, stream_ptr)` works through `cmeta_container_range_view()` with no per-user-type `_Generic` registry.

Do not add generated application-type associations.

- [ ] **Step 3: Change terminals to choose collector from the output handle**

Replace:

```c
collect(stream_ptr, container_type, output_ptr, limit)
to_list(stream_ptr, list_type, output_ptr, limit)
```

with an instance-driven form equivalent to:

```c
collect(stream_ptr, output_ptr, limit)
to_list(stream_ptr, output_ptr, limit)
```

The collector comes from `cmeta_container_descriptor(output_ptr)->collector`.

- [ ] **Step 4: Run stream tests**

Verify source mutation detection, successful collect, bounded collect failure, and output abort semantics.

- [ ] **Step 5: Commit**

```bash
git add turbostl/include/turbostl/stream.h turbostl/tests cflow
 git commit -m "refactor(turbostl): remove stream type tokens"
```

---

### Task 6: Complete Natural Symbol and Source-File Rename

**Files:**
- Rename remaining `turbostl/src/turbo_*.c` files to natural names
- Rename remaining internal TurboSTL identifiers where they belong to TurboSTL
- Modify: `turbostl/CMakeLists.txt`
- Modify all installed TurboSTL headers

**Interfaces:**
- Final canonical compiled symbols are `vec_*`, `list_*`, `map_*`, `hash_map_*`, `btree_*`, etc.
- Final status is `stl_status` / `STL_*`.

- [ ] **Step 1: Rename implementation files without changing algorithms**

Examples:

```text
turbo_vec.c       -> vec.c
turbo_list.c      -> list.c
turbo_map.c       -> map.c
turbo_hash_map.c  -> hash_map.c
turbo_btree.c     -> btree.c
```

Update CMake source lists in the same commit.

- [ ] **Step 2: Remove temporary `turbo_*` typedef/function aliases from installed headers**

Do not retain compatibility aliases after repository consumers are migrated.

- [ ] **Step 3: Preserve known contracts while renaming**

In particular:

- heap typed ordering still comes from `element_type->traits->compare`;
- raw heap bytes still accept comparator/context;
- stable sort remains `stable_sort(base, count, type, scratch_byte_limit)` and uses `type->traits->compare`;
- no algorithm signature is changed merely because the symbol is renamed.

- [ ] **Step 4: Build `turbo_stl` independently**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_stl
```

Expected: PASS with only `TurboUtils::CMeta` as the base module dependency.

- [ ] **Step 5: Commit**

```bash
git add turbostl
 git commit -m "refactor(turbostl): finish natural symbol migration"
```

---

### Task 7: Migrate Repository Consumers to the Self-Describing API

**Files:**
- Modify TurboSTL tests/examples/benchmarks
- Modify `turbo_serial/**` consumers
- Modify `utils/**` / Core consumers when they include TurboSTL
- Modify any CFlow/TurboSTL adapter examples
- Modify install-consumer fixtures under `turbostl/tests/install_consumer`

**Interfaces:**
- Repository consumers use declaration macros + natural handle operations only.

- [ ] **Step 1: Convert existing generated-type call sites**

Replace patterns such as:

```c
typed(Vec, IntVec, int);
IntVec values = {0};
IntVec_init(&values, 16u);
```

with:

```c
Vec(int, values);
vec_init(&values, 16u);
```

Replace equivalent List/Map/etc call sites.

- [ ] **Step 2: Convert remaining explicit-descriptor normal user calls**

Where a CMeta-resolved declaration type is known at compile time, prefer the declaration DSL rather than repeatedly passing `CMETA_TYPEOF(...)` into init.

Keep explicit raw-byte calls only where raw bytes are actually intended.

- [ ] **Step 3: Build all repository targets**

Run:

```bash
cmake --build --preset linux-release-user
```

Fix only real dependency/API migration errors; do not reintroduce compatibility aliases to avoid migrating a caller.

- [ ] **Step 4: Run all registered tests**

Run:

```bash
ctest --preset linux-release-user --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add turbostl turbo_serial utils cflow
 git commit -m "refactor: migrate consumers to self-describing STL"
```

---

### Task 8: Revalidate PR #29 Execution-Foundation Boundaries and Cross-Platform CI

**Files:**
- Modify CMake target dependencies only if compile/link evidence proves a declared dependency is missing
- Do not add Platform/Core dependencies to `TurboUtils::STL`
- Do not duplicate preset compiler flags in workflow YAML

**Interfaces:**
- Validates both the TurboSTL API refactor and the existing Platform -> Concurrency -> CFlow -> Core execution-foundation work.

- [ ] **Step 1: Build module targets independently**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_platform turbo_concurrency turbo_cmeta turbo_cflow turbo_stl turbo_utils
```

Expected: no reverse dependency or missing transitive include/link failures.

- [ ] **Step 2: Verify install consumers**

Install to a temporary prefix and compile consumers linking only `TurboUtils::STL`, only `TurboUtils::CFlow`, and only `TurboUtils::Core` as appropriate. The STL consumer uses `Vec(int, v)` and `vec_init(&v, ...)` without generated type wrappers.

- [ ] **Step 3: Run a clean Linux build/test**

```bash
cmake --preset linux-release-user --fresh
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Expected: zero build errors and zero test failures.

- [ ] **Step 4: Push and inspect fresh Linux/Windows CI**

For each failure, inspect the first real configure/build/test error and fix the owning layer. Do not suppress diagnostics or infer success from queued/skipped jobs.

- [ ] **Step 5: Final public API review**

Confirm:

- no installed TurboSTL header exposes permanent `turbo_*` aliases;
- no TurboSTL user documentation/test requires `IntVec`, `IntList`, `IntMap`, or generated `Type_method` calls;
- declaration macros bind metadata once and normal operations require no type token;
- destroy preserves bindings;
- unresolved CMeta types fail safely;
- `TurboUtils::STL -> TurboUtils::CMeta` remains the base dependency contract;
- CFlow/Stream still uses `cmeta_container_header` and does not depend on generated application container types.

- [ ] **Step 6: Squash mechanical migration history only after green CI**

If PR #29 history contains temporary aliases/placeholder/mechanical commits, squash only after the final tree has passed fresh Linux and Windows CI, then rerun CI for the rewritten head before claiming completion.
