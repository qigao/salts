# TurboSTL Natural API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace every public TurboSTL `turbo_*` container/status symbol with one natural canonical API (`vec_*`, `list_*`, `map_*`, `stl_status`, `STL_*`) while preserving behavior and the `TurboSTL -> CMeta` dependency boundary.

**Architecture:** Public raw container headers become the sole owners of natural container namespaces. Typed CMeta facades keep generated concrete `Type_method` APIs and drop generic `list_init/map_init` macros that would collide with raw names. Compiled implementation symbols and source filenames are mechanically renamed without changing algorithms, then all repository consumers are migrated and verified through compile/link/runtime tests.

**Tech Stack:** C11, C++17 header checks, CMeta generated facades, CMake/Ninja, TinyTest, GitHub Actions Linux/Windows release jobs.

**Spec:** `docs/superpowers/specs/2026-08-22-turbostl-natural-api-design.md`

## Global Constraints

- Final installed TurboSTL headers contain no permanent `turbo_*` container/status aliases.
- Canonical raw naming is `<kind>_t` and `<kind>_<operation>()`, for example `list_t` / `list_init()` and `hash_map_t` / `hash_map_get()`.
- Shared status is `stl_status` with `STL_OK`, `STL_INVALID_ARGUMENT`, `STL_OUT_OF_MEMORY`, `STL_CAPACITY_EXCEEDED`, `STL_EMPTY`, `STL_NOT_FOUND`, `STL_TYPE_MISMATCH`, and `STL_TRAIT_MISSING`.
- Typed facades use generated concrete methods such as `IntList_init()` and `UserMap_put()`; generic `list_init(list_type, ...)` / `map_init(map_type, ...)` macros are removed.
- Base `TurboUtils::STL` remains PUBLIC-dependent only on `TurboUtils::CMeta`; do not add Core, Platform, or Concurrency dependencies.
- `TurboUtils::STLStream` remains the explicit `TurboUtils::STL + TurboUtils::CFlow` adapter target.
- Preserve container algorithms/storage behavior; this is naming and ownership cleanup.
- Keep strict C11 and C++17 public-header compatibility.
- Do not add grep/source-spelling tests; verify through compile/link/runtime evidence and final API review.
- No completion claim until a fresh final Linux and Windows CI run succeeds.

---

### Task 1: Establish Natural Status and Sequence APIs

**Files:**
- Modify: `turbostl/include/turbostl/status.h`
- Modify: `turbostl/include/turbostl/vec.h`
- Modify: `turbostl/include/turbostl/deque.h`
- Modify: `turbostl/include/turbostl/list.h`
- Modify: `turbostl/include/turbostl/stack.h`
- Modify: `turbostl/include/turbostl/queue.h`
- Modify: `turbostl/include/turbostl/heap.h`
- Modify: `turbostl/include/turbostl/sort.h`
- Modify: `turbostl/tests/turbostl_header_test.c`
- Modify: `turbostl/tests/turbostl_header_cpp_test.cpp`
- Modify: `turbostl/tests/turbostl_sequence_test.c`
- Modify: `turbostl/tests/turbostl_list_test.c`

**Interfaces:**
- Produces: `stl_status` and all `STL_*` values.
- Produces raw types/functions: `vec_t/vec_*`, `deque_t/deque_*`, `list_t/list_iter_t/list_*`, `stack_t/stack_*`, `queue_t/queue_*`, `heap_t/heap_*`, `sort_*`.
- Consumes: `cmeta_type_desc`, CMeta traits/range APIs.

- [ ] **Step 1: Make public-header tests use only natural sequence names**

Update the C and C++ header smoke tests so they instantiate at least:

```c
vec_t vec = {0};
deque_t deque = {0};
list_t list = {0};
stack_t stack = {0};
queue_t queue = {0};
heap_t heap = {0};
stl_status status = STL_OK;
```

and reference `vec_init`, `list_init`, `queue_init`, `heap_init`, and `sort` declarations without any `turbo_*` symbol.

- [ ] **Step 2: Verify RED on the current tree**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_header_test turbostl_header_cpp_test
```

Expected: compile failure because `vec_t`, `list_t`, `stl_status`, and natural functions do not exist yet.

- [ ] **Step 3: Rename the shared status contract**

`status.h` becomes exactly one public status namespace:

```c
typedef enum stl_status {
  STL_OK = 0,
  STL_INVALID_ARGUMENT,
  STL_OUT_OF_MEMORY,
  STL_CAPACITY_EXCEEDED,
  STL_EMPTY,
  STL_NOT_FOUND,
  STL_TYPE_MISMATCH,
  STL_TRAIT_MISSING
} stl_status;
```

Do not retain `turbo_stl_status` or `TURBO_STL_*` aliases.

- [ ] **Step 4: Rename raw sequence public declarations**

Apply these canonical prefix mappings consistently in the listed headers:

```text
turbo_vec_t / turbo_vec_*             -> vec_t / vec_*
turbo_deque_t / turbo_deque_*         -> deque_t / deque_*
turbo_list_t / turbo_list_iter_t      -> list_t / list_iter_t
turbo_list_*                           -> list_*
turbo_stack_t / turbo_stack_*         -> stack_t / stack_*
turbo_queue_t / turbo_queue_*         -> queue_t / queue_*
turbo_heap_t / turbo_heap_*           -> heap_t / heap_*
turbo_sort_*                           -> sort_*
turbo_stl_status / TURBO_STL_*        -> stl_status / STL_*
```

`stack.h` wraps `vec_t`; `queue.h` wraps `deque_t`. Inline wrappers call only natural underlying functions.

- [ ] **Step 5: Migrate sequence/list behavior tests**

Update `turbostl_sequence_test.c` and `turbostl_list_test.c` to the natural types/functions/status values without changing test assertions or runtime scenarios.

- [ ] **Step 6: Build focused sequence targets**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_header_test turbostl_header_cpp_test turbostl_sequence_test turbostl_list_test
ctest --preset linux-release-user -R "^(turbostl_header_test|turbostl_header_cpp_test|turbostl_sequence_test|turbostl_list_test)$" --output-on-failure
```

Expected: compile/link/runtime PASS after implementation symbols are made available by Task 2's mechanical bridge/rename; until then header compilation is the required green gate for this task.

- [ ] **Step 7: Commit**

```bash
git add turbostl/include/turbostl/status.h turbostl/include/turbostl/{vec,deque,list,stack,queue,heap,sort}.h turbostl/tests
 git commit -m "refactor(turbostl): expose natural sequence API"
```

---

### Task 2: Rename Compiled Sequence Symbols and Source Files

**Files:**
- Rename: `turbostl/src/turbo_vec.c` -> `turbostl/src/vec.c`
- Rename: `turbostl/src/turbo_deque.c` -> `turbostl/src/deque.c`
- Rename: `turbostl/src/turbo_list.c` -> `turbostl/src/list.c`
- Rename: `turbostl/src/turbo_heap.c` -> `turbostl/src/heap.c`
- Rename: `turbostl/src/turbo_sort.c` -> `turbostl/src/sort.c`
- Modify: the renamed source files
- Modify: `turbostl/CMakeLists.txt`

**Interfaces:**
- Consumes: natural public sequence declarations from Task 1.
- Produces actual linked symbols `vec_*`, `deque_*`, `list_*`, `heap_*`, and `sort_*`.

- [ ] **Step 1: Update source filenames in CMake**

Use:

```cmake
src/vec.c
src/deque.c
src/list.c
src/heap.c
src/sort.c
```

instead of the old `src/turbo_*.c` paths.

- [ ] **Step 2: Mechanically rename sequence implementation identifiers**

Within each renamed source file apply only the corresponding public prefix mapping from Task 1. Keep local algorithm names/logic unchanged except where a local helper itself begins with the same public prefix and must track the owning type.

- [ ] **Step 3: Build sequence implementation and tests**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_stl turbostl_sequence_test turbostl_list_test
ctest --preset linux-release-user -R "^(turbostl_sequence_test|turbostl_list_test)$" --output-on-failure
```

Expected: PASS with no unresolved old `turbo_vec_*`, `turbo_deque_*`, `turbo_list_*`, `turbo_heap_*`, or `turbo_sort_*` symbols.

- [ ] **Step 4: Commit**

```bash
git add turbostl/src turbostl/CMakeLists.txt
 git commit -m "refactor(turbostl): rename sequence implementation symbols"
```

---

### Task 3: Rename Associative and Tree APIs

**Files:**
- Modify: `turbostl/include/turbostl/set.h`
- Modify: `turbostl/include/turbostl/hash_set.h`
- Modify: `turbostl/include/turbostl/map.h`
- Modify: `turbostl/include/turbostl/hash_map.h`
- Modify: `turbostl/include/turbostl/multimap.h`
- Modify: `turbostl/include/turbostl/btree.h`
- Modify: `turbostl/include/turbostl/bplus_tree.h`
- Rename/modify: `turbostl/src/turbo_set.c` -> `turbostl/src/set.c`
- Rename/modify: `turbostl/src/turbo_hash_set.c` -> `turbostl/src/hash_set.c`
- Rename/modify: `turbostl/src/turbo_map.c` -> `turbostl/src/map.c`
- Rename/modify: `turbostl/src/turbo_hash_map.c` -> `turbostl/src/hash_map.c`
- Rename/modify: `turbostl/src/turbo_multimap.c` -> `turbostl/src/multimap.c`
- Rename/modify: `turbostl/src/turbo_btree.c` -> `turbostl/src/btree.c`
- Rename/modify: `turbostl/src/turbo_bplus_tree.c` -> `turbostl/src/bplus_tree.c`
- Rename/modify internal backing file: `turbostl/src/turbo_rbtree.c` -> `turbostl/src/rbtree.c`
- Modify internal backing header: `turbostl/src/turbo_rbtree_internal.h`
- Modify: `turbostl/CMakeLists.txt`
- Modify: `turbostl/tests/turbostl_hash_test.c`
- Modify: `turbostl/tests/turbostl_map_test.c`
- Modify: `turbostl/tests/turbostl_tree_test.c`
- Modify: `turbostl/tests/turbostl_rbtree_test.c`
- Modify: `turbostl/tests/turbostl_ownership_test.c`

**Interfaces:**
- Produces: `set_t/set_*`, `hash_set_t/hash_set_*`, `map_t/map_*`, `hash_map_t/hash_map_*`, `multimap_t/multimap_*`, `btree_t/btree_*`, `bplus_tree_t/bplus_tree_*`.
- Internal-only backing identifiers use `rbtree_*`; there is no new public `rbtree` API unless one already exists in an installed header.

- [ ] **Step 1: Rename associative/tree public declarations**

Apply exactly:

```text
turbo_set_*         -> set_*
turbo_hash_set_*    -> hash_set_*
turbo_map_*         -> map_*
turbo_hash_map_*    -> hash_map_*
turbo_multimap_*    -> multimap_*
turbo_btree_*       -> btree_*
turbo_bplus_tree_*  -> bplus_tree_*
```

including `_t`, iterator/entry/config types, functions, and references to `stl_status/STL_*`.

- [ ] **Step 2: Rename compiled and internal backing identifiers/files**

Apply the same mappings to implementation files and `turbo_rbtree_* -> rbtree_*` internally. Update `turbostl/CMakeLists.txt` to natural source filenames.

- [ ] **Step 3: Migrate associative/tree behavior tests**

Update the listed tests to natural API spellings while preserving existing behavior assertions, ownership scenarios, and warning gates.

- [ ] **Step 4: Build and run focused tests**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_hash_test turbostl_map_test turbostl_tree_test turbostl_rbtree_test turbostl_ownership_test
ctest --preset linux-release-user -R "^(turbostl_hash_test|turbostl_map_test|turbostl_tree_test|turbostl_rbtree_test|turbostl_ownership_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add turbostl/include/turbostl turbostl/src turbostl/tests turbostl/CMakeLists.txt
 git commit -m "refactor(turbostl): expose natural associative API"
```

---

### Task 4: Clean CMeta Typed and Metadata Vocabulary

**Files:**
- Modify: `turbostl/include/turbostl/meta.h`
- Modify: `turbostl/include/turbostl/typed.h`
- Modify: `turbostl/include/turbostl.h`
- Modify: `turbostl/tests/turbostl_typed_test.c`
- Modify: `turbostl/tests/turbostl_header_typed_test.c`
- Modify: `turbostl/tests/turbostl_header_typed_cpp_test.cpp`
- Modify: `turbostl/tests/turbostl_entry_test.c`

**Interfaces:**
- Produces public/internal TurboSTL metadata vocabulary `STL_META_*`, `STL_KIND_ROW_*`, and natural raw prefixes in generated descriptors.
- Preserves CMeta-owned names such as `CMETA_TYPED_Vec`, `CMETA_GENERIC_KIND_Vec`, and `CMETA_*` interfaces.
- Removes generic typed front-end macros `list_init`, `list_add`, `list_pop_front`, `list_clear`, `list_destroy`, `map_init`, `map_put`, `map_clear`, `map_size`, `map_destroy` from `typed.h`.

- [ ] **Step 1: Update typed header tests to use generated concrete methods**

Use a concrete declaration such as:

```c
typed(Vec, IntVec, int);
IntVec vec = {0};
check_equal(IntVec_init(&vec, 8u), CMETA_OK);
check_equal(IntVec_push(&vec, 7), CMETA_OK);
IntVec_destroy(&vec);
```

and equivalent map/list coverage. Do not invoke generic `list_init(...)` or `map_init(...)` macros.

- [ ] **Step 2: Verify RED before metadata rename**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_typed_test turbostl_header_typed_test turbostl_header_typed_cpp_test
```

Expected: failure until metadata rows/prefix mappings target the natural raw APIs.

- [ ] **Step 3: Rename TurboSTL-owned metadata macros**

Apply:

```text
TURBO_META_*          -> STL_META_*
TURBO_STL_KIND_ROW_*  -> STL_KIND_ROW_*
TURBO_*_DEFINE        -> STL_*_DEFINE
```

for TurboSTL-owned generation macros in `meta.h`/`typed.h`, while retaining all `CMETA_*` names because they belong to CMeta.

Update every raw prefix embedded in kind rows:

```text
turbo_vec -> vec
turbo_deque -> deque
turbo_list -> list
turbo_stack -> stack
turbo_queue -> queue
turbo_heap -> heap
turbo_set -> set
turbo_hash_set -> hash_set
turbo_hash_map -> hash_map
turbo_map -> map
turbo_multimap -> multimap
turbo_btree -> btree
turbo_bplus_tree -> bplus_tree
```

- [ ] **Step 4: Remove typed/raw colliding front-end macros**

Delete the generic macro block that defines raw-looking `list_*` and `map_*` front ends. Concrete generated `Type_method` calls are the typed API.

- [ ] **Step 5: Run typed/meta regressions**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_typed_test turbostl_header_typed_test turbostl_header_typed_cpp_test turbostl_entry_test
ctest --preset linux-release-user -R "^(turbostl_typed_test|turbostl_header_typed_test|turbostl_header_typed_cpp_test|turbostl_entry_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add turbostl/include/turbostl/{meta,typed}.h turbostl/include/turbostl.h turbostl/tests
 git commit -m "refactor(turbostl): naturalize typed metadata API"
```

---

### Task 5: Migrate STLStream and Repository Consumers

**Files:**
- Modify: `turbostl/include/turbostl/stream.h`
- Modify all files under: `turbostl/examples/`
- Modify all files under: `turbostl/benchmarks/`
- Modify: `turbo_serial/turbo_serial_internal.h`
- Modify any `turbo_serial/*.c` consumers that call renamed STL APIs
- Modify Core/Utils consumers found by compile errors or direct TurboSTL includes
- Modify CFlow/STL adapter consumers only where they reference renamed raw STL symbols
- Modify: `turbostl/tests/install_consumer/*`

**Interfaces:**
- `TurboUtils::STLStream` continues to expose CFlow-owned `stream()/collect()/to_list()` behavior but binds natural TurboSTL raw/generated types.
- No base `TurboUtils::STL` dependency on CFlow is introduced.

- [ ] **Step 1: Migrate STLStream helper internals**

Update collector/type references to `stl_status/STL_*` and natural raw prefixes. Keep `turbostl_stream_t`/`turbostl_collect_result` unless they conflict with the approved raw naming rule; they are module-adapter names, not the redundant raw `turbo_<container>` prefix.

- [ ] **Step 2: Migrate examples, benchmarks, install consumer, turbo_serial, and Core callers**

Apply the same exact raw prefix mapping used in Tasks 1 and 3. Do not add compatibility aliases to public headers to avoid touching consumers.

- [ ] **Step 3: Build public/install consumers and downstream targets**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_stl turbo_stl_stream turbo_serial turbo_utils
ctest --preset linux-release-user -R "turbostl" --output-on-failure
```

Expected: no downstream unresolved old TurboSTL API references.

- [ ] **Step 4: Commit**

```bash
git add turbostl turbo_serial utils cflow
 git commit -m "refactor(turbostl): migrate natural API consumers"
```

---

### Task 6: Re-establish the Module Boundary Caught by Execution-Foundation CI

**Files:**
- Modify only the TurboSTL files that currently include Core compatibility headers such as `turbo_thread.h` or `platform.h` without owning that dependency.
- Modify: `turbostl/CMakeLists.txt` only to keep the declared dependency set explicit.

**Interfaces:**
- Base target remains:

```cmake
target_link_libraries(turbo_stl PUBLIC TurboUtils::CMeta)
```

- `TurboUtils::STLStream` remains:

```cmake
target_link_libraries(turbo_stl_stream INTERFACE TurboUtils::STL TurboUtils::CFlow)
```

- [ ] **Step 1: Remove accidental Core/Platform compatibility includes from TurboSTL**

If a container implementation only needs standard/CMeta facilities, include those directly. Do not satisfy `turbo_thread.h`/`platform.h` compile errors by linking Platform/Concurrency into `turbo_stl`.

- [ ] **Step 2: Build `turbo_stl` independently**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_stl
```

Expected: PASS with only CMeta as the module dependency.

- [ ] **Step 3: Build CFlow/Core after the boundary correction**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_cflow turbo_utils
```

Expected: PASS; no CFlow -> Core or TurboSTL -> Platform/Concurrency cycle.

- [ ] **Step 4: Commit**

```bash
git add turbostl
 git commit -m "refactor(turbostl): restore module dependency boundary"
```

---

### Task 7: Full Regression, Installed Surface Review, and Final CI

**Files:**
- Modify only files required by real configure/build/test failures.
- Do not add grep-based tests or duplicate preset flags into workflow YAML.

**Interfaces:**
- Final installed API has only natural raw TurboSTL names.

- [ ] **Step 1: Run a clean full Linux configure/build/test**

Run:

```bash
cmake --preset linux-release-user --fresh
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Expected: zero build errors and zero test failures.

- [ ] **Step 2: Build key module targets independently**

Run:

```bash
cmake --build --preset linux-release-user --target turbo_stl turbo_stl_stream turbo_cflow turbo_utils turbo_serial
```

Expected: dependency graph resolves without compatibility-header leakage.

- [ ] **Step 3: Review final installed/public surface**

Manually review `turbostl/include/turbostl/*.h` and `turbostl/include/turbostl.h` to ensure the canonical examples are present:

```text
vec_t / vec_*
list_t / list_*
map_t / map_*
hash_map_t / hash_map_*
stl_status / STL_*
STL_META_* / STL_KIND_ROW_*
```

and no permanent `turbo_<container>` or `TURBO_STL_*` compatibility declarations/aliases remain. This is a review step, not a source-spelling test.

- [ ] **Step 4: Inspect fresh Linux/Windows PR CI**

For each platform, require real Configure/Build/Test steps. Fix the first real owning-module error; distinguish runner/pre-step failures from code failures.

Expected Linux: configure success, full build success, CMeta/CFlow filtered tests success.

Expected Windows: configure/build/test command success.

- [ ] **Step 5: Final history cleanup only after green evidence**

If the PR contains mechanical intermediate commits, squash only after the final tree is green. Re-run CI on the squashed head before claiming completion.

- [ ] **Step 6: Commit any final verification fixes**

```bash
git add -A
 git commit -m "refactor(turbostl): complete natural API migration"
```
