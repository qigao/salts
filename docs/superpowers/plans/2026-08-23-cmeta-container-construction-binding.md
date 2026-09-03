# CMeta Container Construction Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow a reflected Struct field declared as `TYPE(Vec, int)` / `TYPE(Map, int, long)` to bind a completely zero Container handle from static type metadata before the existing Collector runs.

**Architecture:** CMeta gains a lightweight `cmeta_declared_type` that stores storage layout, canonical generic constructor, concrete argument descriptors, and optional construction ops; it does not generate a second `cmeta_type_identity`. `cmeta_container_ext` gains an append-only construction pointer, while Container supplies canonical storage descriptors and transactional unary/binary bind adapters. `Struct` lowers tagged `TYPE(...)` specs to ordinary handle storage and emits TU-local declared metadata.

**Tech Stack:** ISO C11 preprocessor/macros, CMeta type descriptors/type identities, Container handle metadata, TinyTest, CMake/CTest, C++17 public-header compatibility, GitHub Actions Linux/Windows.

**Spec:** `docs/superpowers/specs/2026-08-23-cmeta-container-construction-binding-design.md`

## Global Constraints

- Do not modify CBind, CSerde, parser, DataBind, TBE, OPTIONAL, or `cmeta_data_desc`.
- Do not add semantic T/K/V fields or a runtime type-application registry.
- Do not add shallow whole-container copy/move/destroy traits; nested `TYPE(...)` arguments remain out of grammar in this PR.
- `cmeta_container_ext` changes are append-only; old prefixes through `.type` and `.data` must remain valid.
- Container handle layouts and existing `Vec(T,name)` / `Map(K,V,name)` declaration APIs must not change.
- `cmeta_container_bind_types()` must be allocation-free and transactional.
- Final exact head must pass Linux release fresh configure/build/selected tests and Windows configure/build/test.

---

### Task 1: Establish the RED contract for TYPE fields and zero-handle binding

**Files:**
- Create: `cstl/tests/cstl_construction_binding_test.c`
- Modify: `cstl/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `Struct(...)`, `vec_t`, `map_t`, canonical Container descriptors.
- Produces: failing compile-time contract for `TYPE(...)`, `cmeta_field_desc.declared_type`, and `cmeta_container_bind_types()`.

- [ ] **Step 1: Add the failing test target**

Add to `cstl/tests/CMakeLists.txt`:

```cmake
cmake_add_test(cstl_construction_binding_test
  SOURCES cstl_construction_binding_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

set_target_properties(cstl_construction_binding_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

- [ ] **Step 2: Write the first failing source**

Create `cstl/tests/cstl_construction_binding_test.c` with the minimum contract:

```c
#include <cstl/typed.h>
#include "tinytest.h"

Struct(construction_payload,
    (TYPE(Vec, int), values),
    (TYPE(Map, int, long), index)
);

suite("Container construction binding") {
    it("exposes declared type metadata for container fields") {
        const cmeta_field_desc *values =
            cmeta_struct_find_field(construction_payload_meta(), "values");
        construction_payload payload = {0};

        check_true(values != NULL);
        check_true(values->declared_type != NULL);
        check_equal(cmeta_container_bind_types(
                        &payload.values, values->declared_type),
                    CMETA_OK);
    }
}
```

- [ ] **Step 3: Push the test-only commit and verify RED in CI**

Expected current failure is compile-time and must mention one of the absent production contracts (`TYPE`, `declared_type`, or `cmeta_container_bind_types`). A configure failure unrelated to the new target does not count as RED.

- [ ] **Step 4: Commit**

```bash
git add cstl/tests/CMakeLists.txt \
        cstl/tests/cstl_construction_binding_test.c
git commit -m "test(container): specify declared construction binding"
```

---

### Task 2: Add CMeta declared-type core and cross-language type lookup

**Files:**
- Create: `cmeta/include/cmeta/declared_type.h`
- Create: `cmeta/src/declared_type.c`
- Create: `cmeta/tests/cmeta_declared_type_test.c`
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/include/cmeta/meta.h`
- Modify: `cmeta/CMakeLists.txt`
- Modify: `cmeta/tests/CMakeLists.txt`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Produces:

```c
typedef struct cmeta_declared_type {
    const cmeta_type_desc *storage_type;
    const cmeta_generic_desc *constructor;
    const cmeta_type_desc *const *arguments;
    size_t arity;
    const struct cmeta_container_construct_ops *construction;
} cmeta_declared_type;

bool cmeta_declared_type_valid(const cmeta_declared_type *declared);
bool cmeta_declared_type_constructible(const cmeta_declared_type *declared);
const cmeta_type_desc *cmeta_declared_type_argument(
    const cmeta_declared_type *declared, size_t index);
```

- [ ] **Step 1: Add CMeta RED tests before implementation**

`cmeta_declared_type_test.c` defines a synthetic storage descriptor and generic descriptor:

```c
static const cmeta_type_desc fake_storage = {
    "fake_handle", sizeof(void *), _Alignof(void *), CMETA_T_OBJECT,
    NULL, NULL, NULL
};

static const cmeta_generic_desc fake_vec =
    CMETA_GENERIC_DESC_INIT("test.FakeVec", "FakeVec", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);

static const cmeta_type_desc *const fake_args[] = { &cmeta_type_int };
```

Assert a declared application with `construction == NULL` is valid but not constructible, malformed arity/NULL arguments are invalid, and `cmeta_declared_type_argument()` bounds-checks.

- [ ] **Step 2: Move `CMETA_TYPEOF` to the type core and make it C++17-capable**

Remove the `_Generic`-only definitions from `range.h`. In `cmeta.h`, after type descriptor declarations, define the C path as today and add C++ overloads after the `extern "C"` block:

```c
#ifndef __cplusplus
#define CMETA_TYPE_SELECT(type, fallback_desc) \
    _Generic((type *)0, \
        CMETA_PP_FOR_EACH_A(CMETA_TYPE_ASSOC, ~, CMETA_KNOWN_TYPE_LIST) \
        default: (fallback_desc))
#define CMETA_TYPEOF(type) \
    CMETA_TYPE_SELECT(type, (const cmeta_type_desc *)0)
#else
#define CMETA_CPP_TYPEOF_OVERLOAD(row, ignored) \
    static constexpr const cmeta_type_desc *cmeta_typeof_cpp( \
        CMETA_TYPE_CTYPE(row) *) noexcept { return &CMETA_TYPE_DESC(row); }
CMETA_PP_FOR_EACH_A(CMETA_CPP_TYPEOF_OVERLOAD, ~, CMETA_KNOWN_TYPE_LIST)
#undef CMETA_CPP_TYPEOF_OVERLOAD
static constexpr const cmeta_type_desc *cmeta_typeof_cpp(...) noexcept {
    return nullptr;
}
#define CMETA_TYPEOF(type) cmeta_typeof_cpp((type *)nullptr)
#endif
```

Keep `CMETA_TYPEOF_OR` with equivalent C/C++ fallback behavior.

- [ ] **Step 3: Implement `cmeta_declared_type` validation**

In `declared_type.c`, validate storage/constructor/arity, collect argument identities into a fixed `UINT8_MAX + 1` stack array, and delegate application validity:

```c
bool cmeta_declared_type_valid(const cmeta_declared_type *declared) {
    const cmeta_type_identity *ids[UINT8_MAX + 1u];
    size_t i;
    if (declared == NULL ||
        !cmeta_type_desc_valid(declared->storage_type) ||
        !cmeta_generic_accepts_arity(declared->constructor, declared->arity) ||
        (declared->arity != 0u && declared->arguments == NULL))
        return false;
    for (i = 0u; i < declared->arity; ++i) {
        const cmeta_type_desc *arg = declared->arguments[i];
        if (arg == NULL || !cmeta_type_desc_valid(arg) ||
            cmeta_type_identity_of(arg) == NULL)
            return false;
        ids[i] = cmeta_type_identity_of(arg);
    }
    return cmeta_type_application_valid(
        declared->constructor,
        declared->arity == 0u ? NULL : ids,
        declared->arity);
}
```

`constructible` must require declared validity plus a non-NULL construction pointer; detailed ops ABI validation belongs to Task 4 because the struct is defined there.

- [ ] **Step 4: Export through `cmeta/meta.h`, wire CMake, and test C++ lookup**

Add `src/declared_type.c`, register `cmeta_declared_type_test`, and in `cmeta_header_cpp_test.cpp` assert `CMETA_TYPEOF(int) == &cmeta_type_int` and unknown types resolve to NULL.

- [ ] **Step 5: Run focused tests**

```bash
ctest --test-dir build -R "cmeta_(declared_type|core|type_identity|header_cpp)_test" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add cmeta
git commit -m "feat(cmeta): add declared generic type metadata"
```

---

### Task 3: Teach Struct to lower tagged TYPE specs without Field wrappers

**Files:**
- Modify: `cmeta/include/cmeta/pp.h`
- Modify: `cmeta/include/cmeta/declared_type.h`
- Modify: `cmeta/include/cmeta/struct.h`
- Modify: `cmeta/tests/cmeta_declared_type_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Produces user syntax:

```c
Struct(Payload,
    (int, id),
    (TYPE(Vec, int), values)
);
```

- Consumes provider-macro families formed by appending the kind name to these
  prefixes:

```text
CMETA_DECLARED_STORAGE_
CMETA_DECLARED_STORAGE_DESC_
CMETA_DECLARED_CONSTRUCTOR_
CMETA_DECLARED_CONSTRUCTION_
```

- [ ] **Step 1: Add a tagged-parenthesis probe to `pp.h`**

Use a conventional probe/check pair so Struct can distinguish raw `int` from `(CMETA_TYPE_SPEC_TAG, Vec, int)` without treating arbitrary raw tokens as tuples:

```c
#define CMETA_PP_PROBE() ~, 1
#define CMETA_PP_CHECK_N(x, n, ...) n
#define CMETA_PP_CHECK(...) CMETA_PP_CHECK_N(__VA_ARGS__, 0,)
#define CMETA_PP_IS_PAREN_PROBE(...) CMETA_PP_PROBE()
#define CMETA_PP_IS_PAREN(x) CMETA_PP_CHECK(CMETA_PP_IS_PAREN_PROBE x)
```

Then check the first tuple token against a `CMETA_TYPE_SPEC_TAG` marker before dispatching it as TYPE.

- [ ] **Step 2: Define `TYPE(kind, ...)` as schema metadata, not a C typedef**

In `declared_type.h`:

```c
#define CMETA_TYPE_SPEC_TAG CMETA_TYPE_SPEC_TAG
#define CMETA_TYPE_SPEC(kind, ...) (CMETA_TYPE_SPEC_TAG, kind, __VA_ARGS__)
#ifndef TYPE
#define TYPE(kind, ...) CMETA_TYPE_SPEC(kind, __VA_ARGS__)
#endif
```

Add extractors for kind, arity and arguments plus provider token concatenation. Reject nested TYPE arguments in this PR by allowing only arguments for which `CMETA_TYPEOF(arg)` is non-NULL at runtime validation; no recursive spec lowering is added.

- [ ] **Step 3: Make the Struct declaration pass lower TYPE to provider storage**

Change field declaration from direct `type name;` to a dispatcher conceptually equivalent to:

```c
#define CMETA_STRUCT_FIELD_DECL(type_spec, name) \
    CMETA_STRUCT_STORAGE_TYPE(type_spec) name;
```

For raw types `CMETA_STRUCT_STORAGE_TYPE(int) -> int`; for TYPE specs `TYPE(Vec,int) -> CMETA_DECLARED_STORAGE_Vec -> vec_t`.

- [ ] **Step 4: Add a metadata pre-pass for TYPE fields**

Before `type##__struct_fields[]`, replay rows and emit only for TYPE fields:

```c
static const cmeta_type_desc *const
Payload__values__type_args[] = { &cmeta_type_int };

static const cmeta_declared_type
Payload__values__declared_type = {
    &stl_vec_storage_type,
    &stl_vec_generic_desc,
    Payload__values__type_args,
    1u,
    &stl_vec_construct_ops
};
```

Generate names from owner + member so no application typedef or global registry is needed.

- [ ] **Step 5: Append typed fields to `cmeta_field_desc`**

Append:

```c
const cmeta_type_desc *type;
const cmeta_declared_type *declared_type;
```

Raw fields use `CMETA_TYPEOF(raw_type), NULL`; TYPE fields use provider storage descriptor and address of the generated declared object. Keep the original five fields first and unchanged.

- [ ] **Step 6: Add a CMeta-only synthetic TYPE provider test**

In the test file define a fake provider mapping and a Struct with `TYPE(FakeVec, int)`. Assert storage size/align, `field->type`, constructor, arity and argument. Repeat a minimal compile/read check in C++17.

- [ ] **Step 7: Run tests and commit**

```bash
ctest --test-dir build -R "cmeta_(declared_type|header_cpp|meta_header)_test" --output-on-failure
git add cmeta
git commit -m "feat(cmeta): add TYPE metadata to reflected fields"
```

---

### Task 4: Add append-safe container construction protocol and canonical bind facade

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/src/cstl_type.c`
- Modify: `cmeta/include/cmeta/declared_type.h`
- Modify: `cmeta/tests/cmeta_container_type_test.c`
- Modify: `cmeta/tests/cmeta_declared_type_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Produces:

```c
typedef cmeta_status (*cmeta_container_bind_types_fn)(
    void *object, const cmeta_type_desc *const *arguments, size_t arity);

typedef struct cmeta_container_construct_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_desc *descriptor;
    cmeta_container_bind_types_fn bind_types;
} cmeta_container_construct_ops;

const cmeta_container_construct_ops *
cmeta_container_construction(const void *object);

cmeta_status cmeta_container_bind_types(
    void *object, const cmeta_declared_type *declared);
```

- [ ] **Step 1: Extend tests for three extension prefix generations**

Synthetic extensions must cover:

```text
struct_size through .type         -> data NULL, construction NULL
struct_size through .data         -> data valid, construction NULL
struct_size through .construction -> construction validated
```

Also add malformed ops ABI/NULL descriptor/NULL bind callback tests.

- [ ] **Step 2: Append construction to `cmeta_container_ext`**

Keep current ABI version and append only:

```c
const cmeta_container_construct_ops *construction;
```

Define `CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION = 1u` separately.

- [ ] **Step 3: Implement descriptor-side validation without reading the zero object**

Add a private helper that validates construction ops and obtains the descriptor's `.type` ops directly from `ops->descriptor->ext`, with struct-size checks. Do not call `cmeta_container_extension(object)` from the bind facade before provider dispatch.

- [ ] **Step 4: Implement `cmeta_container_bind_types()`**

Required sequence:

```c
if (object == NULL || !cmeta_declared_type_valid(declared))
    return CMETA_INVALID_ARGUMENT;
ops = declared->construction;
if (!construct_ops_valid(ops))
    return CMETA_INVALID_ARGUMENT;
/* descriptor generic constructor must equal declared constructor by stable id;
   arity must match; arguments were already validated */
return ops->bind_types(object, declared->arguments, declared->arity);
```

Use existing `cmeta_type_identity` generic stable-id semantics or an internal generic-desc equality helper; do not compare constructor addresses as the only identity rule.

- [ ] **Step 5: Tighten `cmeta_declared_type_constructible()`**

Now that construction ops is complete, require valid ABI prefix, descriptor and callback in addition to declared validity.

- [ ] **Step 6: Run CMeta tests and commit**

```bash
ctest --test-dir build -R "cmeta_(declared_type|cstl_type|header_cpp)_test" --output-on-failure
git add cmeta
git commit -m "feat(cmeta): add container construction protocol"
```

---

### Task 5: Register Container TYPE providers and implement unary binding

**Files:**
- Create: `cstl/src/construction_meta.c`
- Modify: `cstl/CMakeLists.txt`
- Modify: `cstl/include/cstl/detail/instance_meta.h`
- Modify: `cstl/include/cstl/typed.h`
- Modify: `cstl/src/generic_meta.c`
- Modify: `cstl/tests/cstl_construction_binding_test.c`

**Interfaces:**
- Produces storage descriptors and construct ops for:
  `Vec, Deque, List, Stack, Queue, Heap, Set, HashSet`.

- [ ] **Step 1: Declare provider symbols**

For each unary kind declare in `detail/instance_meta.h`:

```c
extern const cmeta_type_desc stl_vec_storage_type;
extern const cmeta_container_construct_ops stl_vec_construct_ops;
```

Repeat with canonical kind names.

- [ ] **Step 2: Register TYPE provider macros in `typed.h`**

Example:

```c
#define CMETA_DECLARED_STORAGE_Vec vec_t
#define CMETA_DECLARED_STORAGE_DESC_Vec stl_vec_storage_type
#define CMETA_DECLARED_CONSTRUCTOR_Vec stl_vec_generic_desc
#define CMETA_DECLARED_CONSTRUCTION_Vec stl_vec_construct_ops
```

Repeat for all 13 kinds; Task 6 supplies binary implementations but declarations/macros may be added in one place now.

- [ ] **Step 3: Define canonical storage descriptors in `construction_meta.c`**

Use a macro that emits layout-only descriptors:

```c
#define STL_DEFINE_STORAGE_TYPE(symbol, c_type, display) \
    const cmeta_type_desc symbol = { \
        display, sizeof(c_type), CMETA_ALIGNOF(c_type), CMETA_T_OBJECT, \
        NULL, NULL, NULL \
    }
```

No whole-container traits or application identity.

- [ ] **Step 4: Implement transactional unary bind helpers**

Each binder receives only the zero/uninitialized object and argument descriptors. Validate arity and argument before state inspection. Use kind-specific live predicates:

```text
Vec/Deque/Heap        -> initialized
Stack                 -> raw.initialized
Queue                 -> raw.initialized (deque-backed)
List                  -> impl != NULL
Set                   -> map.impl != NULL
HashSet               -> initialized
```

For each kind distinguish:

```text
all metadata NULL + not live                    -> bind
canonical descriptor + same element + not live -> OK no-op
canonical descriptor + different element       -> TYPE_MISMATCH
partial descriptor/element metadata             -> INVALID_ARGUMENT
live                                             -> INVALID_ARGUMENT
```

Complete all validation before assigning descriptor/element pointer.

- [ ] **Step 5: Define construction ops and append them to existing extensions**

Each ops object points to its canonical `stl_*_container_desc`. Update `STL_DEFINE_UNARY_GENERIC_META` / binary initializer in `generic_meta.c` so `cmeta_container_ext.struct_size` reaches `.construction` and stores the matching ops pointer. Semantic `.data` remains unchanged.

- [ ] **Step 6: Turn the original RED into GREEN for `TYPE(Vec,int)`**

Extend the test to assert:

```c
check_true(values->type == &stl_vec_storage_type);
check_true(values->declared_type->constructor == &stl_vec_generic_desc);
check_equal(values->declared_type->arity, (size_t)1u);
check_true(values->declared_type->arguments[0] == &cmeta_type_int);
check_equal(cmeta_container_bind_types(&payload.values,
                                       values->declared_type), CMETA_OK);
check_true(payload.values.cmeta.descriptor == &stl_vec_container_desc);
check_true(payload.values.element_type == &cmeta_type_int);
```

Then create the existing collector, begin, accept two ints, finish, and assert Vec contents.

- [ ] **Step 7: Add unary matrix + state tests**

Verify all eight unary providers bind the correct slot, same uninitialized binding is idempotent, conflicting type returns `CMETA_TYPE_MISMATCH`, partial state/live state returns `CMETA_INVALID_ARGUMENT`, and a failed call leaves a saved byte copy unchanged.

- [ ] **Step 8: Run tests and commit**

```bash
ctest --test-dir build -R "cstl_(construction_binding|semantic_projection|generic_identity|sequence)_test" --output-on-failure
git add container
git commit -m "feat(container): bind declared unary container types"
```

---

### Task 6: Implement binary construction binding and Map Collector integration

**Files:**
- Modify: `cstl/src/construction_meta.c`
- Modify: `cstl/tests/cstl_construction_binding_test.c`

**Interfaces:**
- Produces binding for `HashMap, Map, MultiMap, BTree, BPlusTree`.

- [ ] **Step 1: Add binary tests first**

`TYPE(Map, int, long)` must expose storage/constructor/two args and bind a zero `map_t` to canonical descriptor + K/V.

Add a matrix for all five binary kinds, including Heap/MultiMap semantic independence already covered by the extension pointer tests.

- [ ] **Step 2: Implement binary transactional bind helper pattern**

Live predicates:

```text
HashMap    -> initialized
Map        -> impl != NULL
MultiMap   -> impl != NULL
BTree      -> initialized
BPlusTree  -> initialized
```

State rules are identical to Task 5 but compare both key and value before any write.

- [ ] **Step 3: Exercise existing ordered Map collector**

After binding `TYPE(Map,int,long)`:

```c
cmeta_collector collector =
    stl_map_container_desc.collector(&payload.index, 4u);
check_equal(cmeta_collector_begin(&collector), CMETA_OK);
```

Accept `cmeta_entry` values with `key_type=&cmeta_type_int` and `value_type=&cmeta_type_long`, finish, then verify `map_get()` returns the inserted value. No Collector source changes are allowed unless a real existing defect is exposed.

- [ ] **Step 4: Run tests and commit**

```bash
ctest --test-dir build -R "cstl_(construction_binding|map|entry|semantic_projection)_test" --output-on-failure
git add cstl/src/construction_meta.c \
        cstl/tests/cstl_construction_binding_test.c
git commit -m "feat(container): bind declared associative container types"
```

---

### Task 7: Lock C++17/ABI regressions and exact-head CI

**Files:**
- Modify: `cstl/tests/cstl_header_typed_cpp_test.cpp`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`
- Modify: `cmeta/tests/cmeta_container_type_test.c`
- Modify: `cstl/tests/CMakeLists.txt` only if stricter warnings are needed

**Interfaces:**
- Produces final public ABI and cross-language proof.

- [ ] **Step 1: Add C++17 TYPE Struct coverage**

In `cstl_header_typed_cpp_test.cpp` declare at namespace scope:

```cpp
Struct(cpp_construction_payload,
    (TYPE(Vec, int), values),
    (TYPE(Map, int, long), index)
);
```

Assert with `static_assert` that member types are `vec_t` / `map_t`, then at runtime inspect `declared_type` and construction ops.

- [ ] **Step 2: Lock extension prefix compatibility**

CMeta tests must explicitly construct old-prefix extensions with `struct_size` through `.type` and `.data`, and assert `cmeta_container_construction()` returns NULL without reading beyond the advertised prefix.

- [ ] **Step 3: Run selected local-equivalent test matrix**

```bash
ctest --test-dir build -R "cmeta_|cstl_(header_typed_cpp|construction_binding|generic_identity|semantic_projection|sequence|map|entry)_test" --output-on-failure
```

- [ ] **Step 4: Scan scope**

The final diff must contain no production changes under CBind/CSerde/parser/DataBind/TBE and no new semantic T/K/V fields. Search the diff for `cmeta_data_desc` modifications and reject any accidental ownership/container-copy implementation.

- [ ] **Step 5: Push exact head and verify GitHub Actions**

Require the exact pushed head SHA to have:

```text
CMeta conformance / Linux release   completed success
CMeta conformance / Windows release completed success
```

Linux must show configure, build, and selected test steps all successful; Windows must show configure/build/test successful.

- [ ] **Step 6: Final commit if test-only fixes were needed**

```bash
git add cmeta/tests cstl/tests
git commit -m "test(cmeta,container): lock construction binding ABI"
```

If no fixes are needed, do not create an empty commit.
