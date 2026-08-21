# CMeta Container Traits Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add finite CMeta type ownership traits, mutation-aware ranges, and transactional collector protocols without implementing any concrete container algorithm.

**Architecture:** CMeta owns semantic descriptors and small function-pointer protocols. Built-in scalar traits are compiled in `cmeta.c`; library/user types provide explicit trait symbols, while Range and collector objects borrow their owners and preserve first-error semantics.

**Tech Stack:** ISO C11, CMeta Schema/Replay and `_Generic`, TinyTest, CMake Presets, MSVC C11, Clang/GCC C11, C++17 header consumers.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- CMeta must not include or link Core, Container, or CFlow.
- Descriptor identity is semantic; pointer equality across translation units is unsupported.
- Custom owning values receive no `memcmp`, byte-hash, address-compare, or shallow-copy fallback.
- Borrowed Range values may not survive mutation, the next cursor step, callback return, or owner destruction.
- Collector `begin` must terminate exactly once through `finish` or `abort`.
- Public headers compile as strict C11 and C++17.
- Use only `rg.exe`/`fd.exe` for repository search and use repository presets for builds.

---

### Task 1: Define CMeta status and type-trait vocabulary

**Files:**
- Create: `cmeta/include/cmeta/status.h`
- Create: `cmeta/include/cmeta/type_traits.h`
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/include/cmeta/meta.h`
- Modify: `cmeta/tests/cmeta_core_test.c`

**Interfaces:**
- Consumes: existing `cmeta_type_desc`, `cmeta_type_kind`, and public C linkage macros.
- Produces: `cmeta_status`, `cmeta_type_traits`, `cmeta_trait_flags`, and `cmeta_type_require_traits()`.

- [ ] **Step 1: Write failing built-in and missing-trait tests**

Add cases that require equality, hash, compare, copy, move, and destroy for `int`, and reject a descriptor with no required capability:

```c
it("exposes explicit built-in scalar traits") {
    int a = 7, b = 7, copied = 0;
    const cmeta_type_traits *traits = cmeta_type_int.traits;
    check_not_null(traits);
    check_true(traits->equal(&a, &b));
    check_equal(traits->compare(&a, &b), 0);
    check_true(traits->copy_construct(&copied, &a));
    check_equal(copied, 7);
}

it("rejects a missing required trait") {
    cmeta_type_desc type = { "opaque", sizeof(int), _Alignof(int), CMETA_T_OBJECT, NULL, NULL };
    check_equal(cmeta_type_require_traits(&type, CMETA_TRAIT_HASH),
                CMETA_TRAIT_MISSING);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test"
```

Expected: compile failure because the new headers, fields, flags, and function do not exist.

- [ ] **Step 3: Add the minimal public types**

Define stable values and signatures:

```c
typedef enum cmeta_status {
    CMETA_OK = 0,
    CMETA_INVALID_ARGUMENT,
    CMETA_TYPE_MISMATCH,
    CMETA_TRAIT_MISSING,
    CMETA_CAPACITY_EXCEEDED,
    CMETA_OUT_OF_MEMORY,
    CMETA_CALLBACK_ERROR
} cmeta_status;

enum {
    CMETA_TRAIT_EQUAL = 1u << 0,
    CMETA_TRAIT_HASH = 1u << 1,
    CMETA_TRAIT_COMPARE = 1u << 2,
    CMETA_TRAIT_COPY = 1u << 3,
    CMETA_TRAIT_MOVE = 1u << 4,
    CMETA_TRAIT_DESTROY = 1u << 5,
    CMETA_TRAIT_TRIVIAL_COPY = 1u << 6,
    CMETA_TRAIT_TRIVIAL_DESTROY = 1u << 7
};

typedef struct cmeta_type_traits {
    uint32_t flags;
    bool (*equal)(const void *, const void *);
    uint64_t (*hash)(const void *);
    int (*compare)(const void *, const void *);
    bool (*copy_construct)(void *, const void *);
    void (*move_construct)(void *, void *);
    void (*destroy)(void *);
} cmeta_type_traits;
```

Append `const cmeta_type_traits *traits` to `cmeta_type_desc`, include both headers from `meta.h`, and implement `cmeta_type_require_traits()` as a pure flag/pointer validator.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test && ctest --preset win-release-user -R ""^cmeta_core_test$"" --output-on-failure"
git add cmeta/include/cmeta/status.h cmeta/include/cmeta/type_traits.h cmeta/include/cmeta/cmeta.h cmeta/include/cmeta/meta.h cmeta/tests/cmeta_core_test.c
git commit -m "feat(cmeta): define explicit type traits"
```

Expected: focused test passes and the commit contains only the trait vocabulary and test.

---

### Task 2: Attach built-in and declared custom traits to descriptors

**Files:**
- Modify: `cmeta/include/cmeta/types.h`
- Modify: `cmeta/include/cmeta/type_traits.h`
- Modify: `cmeta/src/cmeta.c`
- Modify: `cmeta/include/cmeta/struct.h`
- Create: `cmeta/tests/cmeta_traits_peer.c`
- Modify: `cmeta/tests/cmeta_core_test.c`
- Modify: `cmeta/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `cmeta_type_traits` and `cmeta_type_desc::traits`.
- Produces: built-in trait symbols, `Traits(Name, ...)`, five-field `CMETA_TYPE_LIST` rows, and multi-TU semantic equality.

- [ ] **Step 1: Add a failing owning-value and multi-TU test**

Define a counted owning type and explicit traits in the test header scope:

```c
typedef struct owned_int { int *value; } owned_int;
static size_t owned_copies, owned_moves, owned_destroys;
static bool owned_copy(void *dst_, const void *src_);
static void owned_move(void *dst_, void *src_);
static void owned_destroy(void *value_);

Traits(owned_int,
    CMETA_TRAIT_EQUAL | CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    owned_equal, NULL, NULL, owned_copy, owned_move, owned_destroy);
```

Assert the local and peer TU descriptors pass `cmeta_type_equal()` while their addresses differ and both expose the declared flags.

- [ ] **Step 2: Run RED**

Use the Task 1 focused build. Expected: `Traits` and trait-aware row extraction are undefined.

- [ ] **Step 3: Implement finite trait attachment**

Extend type rows to `(TOKEN, C_TYPE, DESCRIPTOR_SYMBOL, KIND, TRAITS_SYMBOL)` and add:

```c
#define CMETA_TYPE_TRAITS(row) CMETA_TYPE_TRAITS_I row
#define CMETA_TYPE_TRAITS_I(tok, ctype, desc, kind, traits) traits

#define Traits(name, flags_, equal_, hash_, compare_, copy_, move_, destroy_) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        (flags_), (equal_), (hash_), (compare_), (copy_), (move_), (destroy_) \
    }
```

Provide named built-in trait objects for `_Bool`, `int`, `long`, `float`, and `double`; update generated descriptors to store `&CMETA_TYPE_TRAITS(row)`. Make row accessors accept the fifth field without changing callable signature enumeration.

- [ ] **Step 4: Verify C, C++, and multi-TU behavior**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test cmeta_header_cpp_test && ctest --preset win-release-user -R ""^(cmeta_core_test|cmeta_header_cpp_test)$"" --output-on-failure"
clang.exe -std=c11 -fsyntax-only -I cmeta/include -I tinytest/include cmeta/tests/cmeta_core_test.c
```

Expected: all tests and syntax checks pass; no descriptor pointer-equality assertion exists.

- [ ] **Step 5: Commit**

```powershell
git add cmeta/include/cmeta/types.h cmeta/include/cmeta/type_traits.h cmeta/include/cmeta/struct.h cmeta/src/cmeta.c cmeta/tests/cmeta_traits_peer.c cmeta/tests/cmeta_core_test.c cmeta/tests/CMakeLists.txt
git commit -m "feat(cmeta): attach traits to type descriptors"
```

---

### Task 3: Make ranges mutation-aware

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/include/cmeta/container.h`
- Modify: `cmeta/tests/cmeta_core_test.c`

**Interfaces:**
- Consumes: semantic type descriptors and existing Range factories.
- Produces: `cmeta_range.version`, `cmeta_range.current_version`, and `CMETA_RANGE_MUTATED`.

- [ ] **Step 1: Write failing generation tests**

Use a fake owner with a mutable generation counter:

```c
it("fails when a borrowed range owner mutates") {
    fake_range_owner owner = { .generation = 4, .values = {1, 2}, .count = 2 };
    cmeta_range range = fake_range(&owner);
    size_t cursor = 0;
    int out = 0;
    check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_VALUE);
    owner.generation++;
    check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_MUTATED);
}
```

- [ ] **Step 2: Run RED**

Expected: compile failure for generation fields and `CMETA_GEN_MUTATED`.

- [ ] **Step 3: Add version validation without owning the source**

Extend `cmeta_range` with a captured `uint64_t version` and optional callback:

```c
typedef uint64_t (*cmeta_range_version_fn)(const void *object);
```

`cmeta_range_next()` compares the current version before calling `next`; unchanged legacy ranges use a NULL callback and retain current behavior. Generated Container range macros accept a version accessor rather than reading a concrete raw layout.

- [ ] **Step 4: Run GREEN and commit**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test && ctest --preset win-release-user -R ""^cmeta_core_test$"" --output-on-failure"
git add cmeta/include/cmeta/range.h cmeta/include/cmeta/container.h cmeta/tests/cmeta_core_test.c
git commit -m "feat(cmeta): detect mutated range owners"
```

---

### Task 4: Add transactional collector protocol

**Files:**
- Create: `cmeta/include/cmeta/collector.h`
- Modify: `cmeta/include/cmeta/container.h`
- Modify: `cmeta/include/cmeta/meta.h`
- Create: `cmeta/tests/cmeta_collector_test.c`
- Modify: `cmeta/tests/CMakeLists.txt`
- Modify: `cmeta/README.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`

**Interfaces:**
- Consumes: `cmeta_status`, `cmeta_type_desc`, and caller-owned zero-initialized output.
- Produces: `cmeta_collector_ops`, `cmeta_collector`, `cmeta_collector_begin/accept/finish/abort`, and an optional collector factory in `cmeta_container_desc`.

- [ ] **Step 1: Write failing transaction-state tests**

```c
it("aborts a begun collector exactly once") {
    fake_collector_state state = {0};
    cmeta_collector collector = fake_collector(&state, &cmeta_type_int, 2);
    int one = 1, two = 2, three = 3;
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &one), CMETA_OK);
    check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &two), CMETA_OK);
    check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &three),
                CMETA_CAPACITY_EXCEEDED);
    cmeta_collector_abort(&collector);
    check_equal(state.abort_count, 1u);
    check_equal(state.finish_count, 0u);
}
```

Also test accept-before-begin, finish-twice, type mismatch, zero limit, and successful finish.

- [ ] **Step 2: Run RED**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cmeta_collector_test"
```

Expected: target or collector API is missing.

- [ ] **Step 3: Implement the explicit state machine**

Use a small enum and isolate adapter callbacks:

```c
typedef enum cmeta_collector_state {
    CMETA_COLLECTOR_ZERO,
    CMETA_COLLECTOR_BEGUN,
    CMETA_COLLECTOR_COMMITTED,
    CMETA_COLLECTOR_ABORTED
} cmeta_collector_state;

typedef struct cmeta_collector_ops {
    cmeta_status (*begin)(void *context, const cmeta_type_desc *input, size_t limit);
    cmeta_status (*accept)(void *context, const void *value);
    cmeta_status (*finish)(void *context);
    void (*abort)(void *context);
} cmeta_collector_ops;
```

The facade validates state and type before dispatch, preserves the first status, and makes repeated abort harmless without calling the adapter twice.

- [ ] **Step 4: Run focused and adjacent tests**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_collector_test cmeta_core_test cmeta_header_cpp_test && ctest --preset win-release-user -R ""^cmeta_"" --output-on-failure"
```

Expected: all CMeta tests pass.

- [ ] **Step 5: Scan, document, and commit**

```powershell
rg.exe -n "memcmp|fallback|placeholder" cmeta/include/cmeta/type_traits.h cmeta/include/cmeta/collector.h cmeta/README.md
git diff --check -- cmeta
git add cmeta
git commit -m "feat(cmeta): add transactional container collectors"
```

Expected: no custom-type fallback or placeholder text; commit contains only CMeta protocol work.
