# TurboSTL Self-Describing Natural API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `Vec(T,name)`, `List(T,name)`, `Map(K,V,name)`, and the other declaration DSLs bind CMeta descriptors directly into ordinary container handles so users call only `vec_*`, `list_*`, `map_*`, etc. and never see generated `IntVec`/`IntMap` types or methods.

**Architecture:** Each container kind has one runtime handle type (`vec_t`, `list_t`, `map_t`, ...). Declaration macros bind type descriptors into that handle without allocating storage. Natural operations consume the bound metadata; compiled storage entry points live behind explicit `*_raw_*` names. CMeta remains the type/traits source, but TurboSTL no longer exposes generated named wrapper types as its public model.

**Tech Stack:** strict C11, C++17 public-header checks, CMeta type descriptors/traits/range/collector APIs, CMake/Ninja, TinyTest, GitHub Actions Linux/Windows release jobs.

**Spec:** `docs/superpowers/specs/2026-08-22-turbostl-natural-api-design.md`

## Global Constraints

- Canonical user declarations are `Vec(T,name)`, `Deque(T,name)`, `List(T,name)`, `Stack(T,name)`, `Queue(T,name)`, `Heap(T,name)`, `Set(T,name)`, `HashSet(T,name)`, `Map(K,V,name)`, `HashMap(K,V,name)`, `MultiMap(K,V,name)`, `BTree(K,V,name)`, and `BPlusTree(K,V,name)`.
- Declaration DSLs bind metadata only; they do not allocate storage or initialize runtime state.
- Canonical user operations are natural instance-driven functions such as `vec_init(&v,n)`, `vec_push(&v,&value)`, `list_init(&l,n)`, and `map_put(&m,&key,&value)`.
- Public TurboSTL usage must not require a generated type token or generated method name such as `IntVec`, `IntVec_init`, `IntList_push_back`, or `UserMap_put`.
- `vec_t`, `list_t`, `map_t`, etc. are the only runtime handle types for their container kinds.
- Shared status is `stl_status` with `STL_*` values.
- Internal compiled typed-storage entry points use explicit `*_raw_*` names. Raw byte APIs (`*_init_bytes`, etc.) remain intentionally explicit user APIs where already supported.
- `destroy()` releases runtime storage but preserves declaration-time type binding so the same handle can be initialized again.
- Base `TurboUtils::STL` remains dependent only on `TurboUtils::CMeta`; do not add Core, Platform, Concurrency, or CFlow dependencies.
- `TurboUtils::STLStream` is the optional CFlow adapter target.
- Preserve container algorithms/storage behavior; this work changes API shape and ownership, not algorithms.
- Keep strict C11 and C++17 public-header compatibility; do not introduce GNU `typeof` or compiler-specific generic inference.
- Do not add grep/source-spelling tests. Use compile/link/runtime evidence and final API review.
- No completion claim until fresh Linux and Windows CI pass on the final head.

---

### Task 1: Remove Generated Named Types from the Public TurboSTL Path

**Files:**
- Modify: `turbostl/include/turbostl/typed.h`
- Modify: `turbostl/tests/turbostl_header_typed_test.c`
- Modify: `turbostl/tests/turbostl_header_typed_cpp_test.cpp`
- Keep migration-only: `turbostl/tests/legacy_generated_typed.h`

**Interfaces:**
- Produces: declaration DSLs only from `turbostl/typed.h`.
- Keeps generated named wrappers available only through the test-only migration header while old regression tests are being migrated.

- [ ] **Step 1: Lock the public declaration contract in tests**

The C test must compile and run code shaped exactly like:

```c
Vec(int, vec);
List(int, list);
Map(int, int, map);
int value = 7;
int key = 1;

check_equal(vec_init(&vec, 8u), STL_OK);
check_equal(vec_push(&vec, &value), STL_OK);
check_equal(list_init(&list, 8u), STL_OK);
check_equal(list_push_back(&list, &value, NULL), STL_OK);
check_equal(map_init(&map, 8u), STL_OK);
check_equal(map_put(&map, &key, &value), STL_OK);

map_destroy(&map);
list_destroy(&list);
vec_destroy(&vec);
```

The C++17 header test must instantiate the same DSL without generated names.

- [ ] **Step 2: Verify the current self-describing test baseline**

Run:

```bash
cmake --build --preset linux-release-user --target turbostl_header_typed_test turbostl_header_typed_cpp_test
ctest --preset linux-release-user -R "^(turbostl_header_typed_test|turbostl_header_typed_cpp_test)$" --output-on-failure
```

Expected on the current checkpoint: PASS. This is the regression baseline before deleting legacy public generation macros.

- [ ] **Step 3: Remove generated-kind registration/generation macros from public `typed.h`**

Delete the public `CMETA_GENERIC_KIND_*` and `CMETA_TYPED_*` blocks from `turbostl/typed.h`. Keep only includes plus the self-describing declaration DSL. Do not modify CMeta's generic generation infrastructure itself.

- [ ] **Step 4: Re-run focused tests**

Run the Step 2 commands again. Expected: PASS without `IntVec`/`IntList`/`IntMap` generation coming from a public TurboSTL header.

- [ ] **Step 5: Commit**

```bash
git add turbostl/include/turbostl/typed.h turbostl/tests
 git commit -m "refactor(turbostl): hide generated container types"
```

---

### Task 2: Make Self-Describing Initialization the Sole Natural Typed Path

**Files:**
- Modify: `turbostl/include/turbostl/{vec,deque,list,stack,queue,heap,set,hash_set,hash_map,map,multimap,btree,bplus_tree}.h`
- Modify corresponding `turbostl/src/*.c` implementation files.
- Modify: `turbostl/tests/turbostl_header_test.c`

**Interfaces:**
- Produces public initializers `kind_init(kind_t *, size_t)` and typed `from_array/from_arrays` forms that read descriptors from the handle.
- Produces internal compiled bridges named `kind_raw_init(...)`, `kind_raw_from_array(...)`, and `kind_raw_destroy_storage(...)` where a bridge is needed during migration.

- [ ] **Step 1: Add/review re-initialization tests for every kind**

For each DSL-declared handle, test `init -> use -> destroy -> init -> destroy` without re-supplying type descriptors. Expected binding persistence is part of the public contract.

- [ ] **Step 2: Rename migration bridges from semantic-looking names to explicit raw names**

Apply mappings such as:

```text
vec_init_with_type       -> vec_raw_init
vec_from_array_with_type -> vec_raw_from_array
vec_destroy_storage      -> vec_raw_destroy_storage

list_init_with_type       -> list_raw_init
map_init_with_types       -> map_raw_init
map_from_arrays_with_types -> map_raw_from_arrays
```

Repeat consistently for every container kind that currently exposes a `*_with_type(s)`/`*_storage` bridge.

- [ ] **Step 3: Keep natural wrappers instance-driven**

Public functions remain:

```c
stl_status vec_init(vec_t *vec, size_t limit);
stl_status list_init(list_t *list, size_t limit);
stl_status map_init(map_t *map, size_t limit);
```

They validate the declaration-time binding, call the raw bridge, and restore the binding after destroy/failure as required.

- [ ] **Step 4: Build and run full TurboSTL tests**

```bash
cmake --build --preset linux-release-user --target turbo_stl
ctest --preset linux-release-user -R "^turbostl_" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add turbostl/include/turbostl turbostl/src turbostl/tests
 git commit -m "refactor(turbostl): separate natural and raw container APIs"
```

---

### Task 3: Migrate Legacy Generated-Type Regression Tests to the Instance Model

**Files:**
- Modify: all `turbostl/tests/*.c` and `*.cpp` that include `legacy_generated_typed.h` or call generated `Type_method` functions.
- Delete after migration: `turbostl/tests/legacy_generated_typed.h`
- Modify: `turbostl/tests/CMakeLists.txt` only if test source membership changes.

**Interfaces:**
- Consumes only the public self-describing DSL and natural operations.
- Removes TurboSTL's last internal dependency on generated named container types.

- [ ] **Step 1: Convert one representative sequence, associative, and tree regression**

Use `Vec/List/Map/BTree` declarations and natural calls while preserving the existing behavior assertions exactly.

- [ ] **Step 2: Run those focused tests**

Expected: identical runtime behavior with no generated wrapper type in the test source.

- [ ] **Step 3: Convert the remaining TurboSTL regression tests**

Do not weaken ownership, trait, range, collector, capacity, or iterator assertions.

- [ ] **Step 4: Delete `legacy_generated_typed.h`**

Only after no TurboSTL regression includes it.

- [ ] **Step 5: Run all TurboSTL tests**

```bash
ctest --preset linux-release-user -R "^turbostl_" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add turbostl/tests
 git commit -m "test(turbostl): migrate regressions to inferred handles"
```

---

### Task 4: Remove Temporary `turbo_*` Container Aliases and Rename Compiled Files/Symbols

**Files:**
- Modify all installed headers under `turbostl/include/turbostl/`.
- Rename compiled source files under `turbostl/src/` from `turbo_*.c` to natural names.
- Modify: `turbostl/CMakeLists.txt`
- Modify all repository consumers in `utils/`, `turbo_serial/`, `cflow/`, examples, and benchmarks that still use old TurboSTL spellings.

**Interfaces:**
- Final installed public API contains only natural container/status names.

- [ ] **Step 1: Migrate repository consumers to natural names**

Replace old TurboSTL type/function/status usage with `vec_*`, `list_*`, `map_*`, `stl_status`, and `STL_*` without changing behavior.

- [ ] **Step 2: Remove temporary aliases from installed headers**

Delete migration typedefs/macros such as:

```c
typedef vec_t turbo_vec_t;
#define turbo_vec_push vec_push
```

and equivalents for every container/status symbol.

- [ ] **Step 3: Rename source files and implementation symbols**

Rename `turbostl/src/turbo_vec.c -> vec.c`, etc., and update `turbostl/CMakeLists.txt`. Keep algorithms unchanged.

- [ ] **Step 4: Build full repository**

```bash
cmake --preset linux-release-user --fresh
cmake --build --preset linux-release-user
```

Expected: PASS with no consumer relying on the removed aliases.

- [ ] **Step 5: Run tests**

```bash
ctest --preset linux-release-user --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add turbostl utils turbo_serial cflow
 git commit -m "refactor(turbostl): remove legacy turbo container API"
```

---

### Task 5: Make CFlow Stream/Collector Integration Instance-Driven

**Files:**
- Modify: `turbostl/include/turbostl/stream.h`
- Modify: container Range/collector adapters as needed.
- Modify TurboSTL stream tests/examples.

**Interfaces:**
- Produces terminals shaped like `to_list(&pipeline, &output, limit)` rather than `to_list(&pipeline, OutputList, &output, limit)`.
- Uses metadata carried by the output handle.

- [ ] **Step 1: Write/update stream tests using self-describing output handles**

Example target shape:

```c
List(int, output);
result = to_list(&pipeline, &output, 32u);
```

No generated output type token is supplied.

- [ ] **Step 2: Verify RED against the old terminal macro**

Expected: compile failure until the terminal/collector path is instance-driven.

- [ ] **Step 3: Change collector lookup to use the output handle descriptor/instance adapter**

Do not add `_Generic` entries for application-generated container types. Dispatch only on finite container handle kinds or through the handle's CMeta descriptor.

- [ ] **Step 4: Run stream tests**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add turbostl/include/turbostl/stream.h turbostl/tests
 git commit -m "refactor(turbostl): make stream collection instance driven"
```

---

### Task 6: Installed-Consumer and Cross-Platform Verification

**Files:**
- Modify install-consumer fixtures only if their user API needs migration.
- Do not duplicate preset flags into workflow YAML.

**Interfaces:**
- Validates the final external TurboSTL contract.

- [ ] **Step 1: Build/install from a clean Linux tree**

```bash
cmake --preset linux-release-user --fresh
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Expected: all PASS.

- [ ] **Step 2: Verify installed external consumer**

External fixture must include installed TurboSTL headers, link `TurboUtils::STL`, declare `Vec/List/Map` handles, and use only natural operations.

- [ ] **Step 3: Final API/diff review**

Verify installed headers have no permanent TurboSTL `turbo_*` container/status aliases, no public generated named-container requirement, and no Core/Platform/Concurrency dependency added to `TurboUtils::STL`.

- [ ] **Step 4: Inspect fresh Linux and Windows CI**

Both jobs must complete configure/build/tests successfully on the final head before completion is claimed.

- [ ] **Step 5: Squash mechanical migration history only after green CI**

If squashing, rerun CI for the rewritten final head before claiming success.
