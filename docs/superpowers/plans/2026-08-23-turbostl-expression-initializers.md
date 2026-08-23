# TurboSTL Expression Initializers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add allocation-free expression initializers for every public TurboSTL container while preserving the existing declaration DSL.

**Architecture:** Each container kind gets one internal brace initializer as its metadata-binding fact source. Existing `Vec(T,name)` / `Map(K,V,name)` declarations consume that initializer, while new `VecOf(T)` / `MapOf(K,V)` macros wrap it in a C11 compound literal so it can be assigned, returned, or passed as a value without generating a new C type.

**Tech Stack:** Strict C11, CMeta finite type descriptors, TurboSTL self-describing handles, TinyTest, CMake Presets, MSVC C mode.

**Spec:** `docs/superpowers/specs/2026-08-23-cmeta-container-construction-binding-design.md`

## Global Constraints

- Preserve the existing one-handle-type-per-container-kind architecture and runtime descriptor ownership.
- Expression initializers perform no allocation and bind only the canonical container descriptor plus existing `CMETA_TYPEOF(...)` arguments.
- Preserve all existing `Vec(T,name)` through `BPlusTree(K,V,name)` source behavior.
- Unknown CMeta types remain explicit `NULL` descriptors and continue to fail at the existing init boundary.
- Do not change raw container algorithms, capacity, allocation, ownership, error, or shutdown behavior.

---

### Task 1: Public expression initializer surface

**Files:**
- Modify: `turbostl/tests/turbostl_header_typed_test.c`
- Modify: `turbostl/include/turbostl/typed.h`

**Interfaces:**
- Consumes: existing `CMETA_TYPEOF(T)`, canonical `stl_*_container_desc` objects, and public handle types.
- Produces: `VecOf(T)`, `DequeOf(T)`, `ListOf(T)`, `StackOf(T)`, `QueueOf(T)`, `HeapOf(T)`, `SetOf(T)`, `HashSetOf(T)`, `HashMapOf(K,V)`, `MapOf(K,V)`, `MultiMapOf(K,V)`, `BTreeOf(K,V)`, and `BPlusTreeOf(K,V)`.

- [x] **Step 1: Write the failing TinyTest behavior**

Add two cases to `turbostl_header_typed_test.c`: one assigns every unary `*Of(T)` value and verifies `cmeta_container_type_application_valid`, and one does the same for all binary `*Of(K,V)` values. Use assignment for `VecOf` and `MapOf` so an initializer-list-only macro cannot satisfy the contract.

- [x] **Step 2: Run the focused target and verify RED**

Run:

```bat
cmake --build --preset build-release-windows --target turbostl_header_typed_test
```

Expected: compilation fails because `VecOf`, `MapOf`, and the other expression initializer macros are not defined.

- [x] **Step 3: Implement the minimal initializer surface**

In `turbostl/typed.h`, define kind-specific brace initializer facts and guarded public `*Of` compound-literal macros. Refactor existing declaration macros to consume the same kind-specific brace initializer without changing their names or resulting handle layouts.

- [x] **Step 4: Run focused GREEN verification**

Run:

```bat
cmake --build --preset build-release-windows --target turbostl_header_typed_test
ctest --preset test-release-windows -R "^turbostl_header_typed_test$" --output-on-failure
```

Expected: target builds and the focused test reports zero failures.

### Task 2: Documentation and adjacent regression

**Files:**
- Modify: `turbostl/README.md`

**Interfaces:**
- Consumes: the declaration and expression APIs from Task 1.
- Produces: public examples and a compatibility statement for both forms.

- [x] **Step 1: Document the two initialization forms**

Replace stale generated-wrapper examples with the current instance declarations and expression initializers:

```c
Vec(int, declared_values);
vec_t assigned_values = VecOf(int);
Map(int, long, declared_scores);
map_t assigned_scores = MapOf(int, long);
```

State that both bind metadata only, allocate nothing, and still require the ordinary `*_init` / `*_destroy` lifecycle.

- [x] **Step 2: Run adjacent C and C++ header regressions**

Run:

```bat
cmake --build --preset build-release-windows --target turbostl_header_typed_test turbostl_header_typed_cpp_test turbostl_construction_binding_test turbostl_typed_test
ctest --preset test-release-windows -R "^(turbostl_header_typed_test|turbostl_header_typed_cpp_test|turbostl_construction_binding_test|turbostl_typed_test)$" --output-on-failure
```

Expected: all four tests pass with zero failures.

- [x] **Step 3: Review the diff and commit**

Run `git diff --check`, inspect `git diff`, then commit the plan, test, header, and documentation with message `feat(turbostl): add expression initializers`.
