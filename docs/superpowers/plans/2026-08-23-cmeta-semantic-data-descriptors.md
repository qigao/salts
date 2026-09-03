# CMeta Semantic Data Descriptors Implementation Plan

> **Status:** Historical execution record. C0/C1/C2 are present on `master`;
> unchecked boxes are retained for traceability, not as an active checklist.
> Current public API, tests, and presets supersede stale examples in this plan.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a versioned, format-neutral CMeta semantic descriptor layer and project proven Container generic applications into `SEQUENCE` / `SET` / `MAP` without creating a second source of truth for `T/K/V`.

**Architecture:** `CMETA_TYPE_APPLY` remains the sole owner of type constructor/arity/arguments. Semantic descriptors describe meaning only. Container links its canonical container extension to one shared semantic category descriptor; callers still obtain concrete type arguments only through `cmeta_container_type_argument()`. Before appending semantic metadata to `cmeta_container_ext`, make its existing `struct_size` checks truly prefix-safe.

**Tech Stack:** C11, C++17 public-header compatibility, CMake presets, TinyTest, Salts::CMeta, Salts::CSTL.

**Spec:** `docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md`, `docs/superpowers/specs/2026-08-23-serialization-data-binding-generic-foundation-amendment.md`, and `docs/superpowers/specs/2026-08-23-serialization-data-binding-semantic-foundation-amendment.md`.

## Global Constraints

- Base implementation starts from `master` at or after `93e68ef443b86aa887cff21d2ceeb134ad32e0e4` (`#37`).
- `CMETA_TYPE_APPLY` owns concrete type arguments. Semantic container descriptors contain no `element`, `key`, or `value` type field.
- Type well-formedness remains separate from operation capability/traits.
- `SET` is a first-class semantic kind.
- `OPTIONAL` is not in this plan. It remains blocked until a typed Option value has a real `CMETA_TYPE_APPLY` identity.
- Field presence, aliases, external names, defaults, nullable policy and emit policy remain CBind/schema concerns.
- No CSerde, CBind, parser, DataBind, TBE, or format-specific production code is introduced.
- No Container raw algorithm, ownership rule, Range traversal, or Collector behavior is rewritten.
- Semantic category projection is independent of runtime generic validity. A raw-byte canonical Vec may project as `SEQUENCE`, but it has no T and cannot enter typed binding; typed consumers must validate the generic application separately.
- Typed Heap and MultiMap instances remain semantically unresolved in v1.
- Nested zero-initialized `vec_t/map_t` struct fields are not solved here; static field TYPE application + construction is the next plan.
- v1 does not reserve an unspecified `cmeta_data_ops` callback pointer.
- Semantic fingerprinting is deferred until the descriptor graph contract is stable.
- `cmeta_container_ext` and `cmeta_data_desc` must both use append-only prefix-size validation; do not validate either with `struct_size >= sizeof(current_struct)`.
- Public CMeta/Container headers compile as C11 and C++17.
- Linux verification uses public repository preset `linux-release-user`; Windows must pass repository CI on the exact final head.

---

## PR decomposition

```text
PR C0  append-only cmeta_container_ext ABI hardening
PR C1  CMeta semantic descriptor core
PR C2  Container semantic projection via cmeta_container_ext.data
```

C1 starts only after C0 merges. C2 starts only after C1 merges. Do not stack production PRs on unmerged heads.

---

# PR C0 — Append-only container extension ABI

## Task 1: Introduce one prefix-safe extension accessor

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/src/cstl_type.c`
- Modify: `cmeta/tests/cmeta_container_type_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Consumes: existing `cmeta_container_desc.ext`, `cmeta_container_ext.type`, and `cmeta_container_type_ops.argument`.
- Produces:

```c
const cmeta_container_ext *cmeta_container_extension(const void *object);
```

This accessor validates only the v1 prefix through `ext.type`. Existing type-ops validation likewise validates only through `ops.argument`.

- [ ] **Step 1: Write the failing public-accessor test**

Add to `cmeta/tests/cmeta_container_type_test.c`, reusing the existing synthetic typed sequence:

```c
it("exposes a validated container extension prefix") {
    cmeta_test_sequence sequence = {
        .cmeta = {&cmeta_test_sequence_desc},
        .element_type = &cmeta_type_int
    };

    check_true(cmeta_container_extension(&sequence) ==
               &cmeta_test_sequence_ext);
    check_null(cmeta_container_extension(NULL));
}
```

- [ ] **Step 2: Build the focused target and verify RED**

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --target cmeta_container_type_test
```

Expected: compile failure because `cmeta_container_extension()` does not exist.

- [ ] **Step 3: Declare the accessor and use explicit field-end sizes**

Declare in `cmeta/include/cmeta/range.h` before the existing type helpers:

```c
const cmeta_container_ext *cmeta_container_extension(const void *object);
```

In `cmeta/src/cstl_type.c`, define a file-local helper:

```c
#define CMETA_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))
```

Implement:

```c
const cmeta_container_ext *cmeta_container_extension(const void *object) {
    const cmeta_container_desc *desc;
    const cmeta_container_ext *ext;

    if (object == NULL) return NULL;
    desc = cmeta_container_descriptor(object);
    if (desc == NULL || desc->ext == NULL) return NULL;
    ext = desc->ext;
    if (ext->abi_version != CMETA_CONTAINER_EXT_ABI_VERSION ||
        ext->struct_size < CMETA_FIELD_END(cmeta_container_ext, type))
        return NULL;
    return ext;
}
```

Refactor `cmeta_container_type_ops_of()` to use this accessor and replace:

```c
ops->struct_size < sizeof(*ops)
```

with:

```c
ops->struct_size < CMETA_FIELD_END(cmeta_container_type_ops, argument)
```

Do not change generic validity semantics or inspect traits.

- [ ] **Step 4: Add a reported-prefix-size regression**

Build a local extension whose reported size ends exactly at `type`:

```c
cmeta_container_ext ext = cmeta_test_sequence_ext;
ext.struct_size =
    offsetof(cmeta_container_ext, type) + sizeof(ext.type);
```

Attach it to a local descriptor and require:

```c
check_not_null(cmeta_container_extension(&sequence));
check_true(cmeta_container_type_application_valid(&sequence));
```

Also clone `cmeta_container_type_ops`, report exactly through `argument`, and require the same application to remain valid.

- [ ] **Step 5: Add C++17 surface coverage**

In `cmeta/tests/cmeta_header_cpp_test.cpp`, include `<cmeta/range.h>` and require:

```cpp
static_assert(std::is_standard_layout_v<cmeta_container_ext>);
static_assert(std::is_standard_layout_v<cmeta_container_type_ops>);
```

Construct prefix-sized values under C++17 so the public layout/API compiles without `_Generic`.

- [ ] **Step 6: Verify GREEN**

```bash
cmake --build --preset linux-release-user --target \
  cmeta_container_type_test cmeta_header_cpp_test
ctest --preset linux-release-user \
  -R '^cmeta_(cstl_type|header_cpp)_test$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add \
  cmeta/include/cmeta/range.h \
  cmeta/src/cstl_type.c \
  cmeta/tests/cmeta_container_type_test.c \
  cmeta/tests/cmeta_header_cpp_test.cpp
git commit -m "refactor(cmeta): make container extensions append-safe"
```

### PR C0 verification gate

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^(cmeta_|cflow_|cstl_)' \
  --output-on-failure
git diff --check
```

Windows CI must pass. No semantic descriptor or Container production file belongs in C0.

---

# PR C1 — CMeta semantic descriptor core

## Task 2: Define the semantic descriptor public contract

**Files:**
- Create: `cmeta/include/cmeta/data.h`
- Create: `cmeta/tests/cmeta_data_test.c`
- Modify: `cmeta/include/cmeta/meta.h`
- Modify: `cmeta/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cmeta_type_desc`, `cmeta_enum_desc`, `cmeta_struct_desc`.
- Produces:

```c
typedef enum cmeta_data_kind {
    CMETA_DATA_BOOL,
    CMETA_DATA_SINT,
    CMETA_DATA_UINT,
    CMETA_DATA_FLOAT,
    CMETA_DATA_STRING,
    CMETA_DATA_BYTES,
    CMETA_DATA_ENUM,
    CMETA_DATA_STRUCT,
    CMETA_DATA_VARIANT,
    CMETA_DATA_SEQUENCE,
    CMETA_DATA_SET,
    CMETA_DATA_MAP,
    CMETA_DATA_CUSTOM
} cmeta_data_kind;

enum { CMETA_DATA_DESC_ABI_VERSION = 1u };

typedef struct cmeta_data_desc {
    size_t struct_size;
    uint32_t abi_version;
    const char *stable_id;
    const char *display_name;
    cmeta_data_kind kind;
    const cmeta_type_desc *storage_type;
    const void *shape;
} cmeta_data_desc;
```

No `CMETA_DATA_OPTIONAL` and no `cmeta_data_ops` are introduced.

Kind-specific immutable shapes:

```c
typedef struct cmeta_data_integer_shape {
    uint8_t bits;
} cmeta_data_integer_shape;

typedef struct cmeta_data_float_shape {
    uint8_t bits;
} cmeta_data_float_shape;

typedef enum cmeta_data_buffer_ownership {
    CMETA_DATA_BUFFER_OWNED,
    CMETA_DATA_BUFFER_BORROWED,
    CMETA_DATA_BUFFER_CUSTOM
} cmeta_data_buffer_ownership;

typedef struct cmeta_data_buffer_shape {
    cmeta_data_buffer_ownership ownership;
} cmeta_data_buffer_shape;

typedef struct cmeta_data_enum_shape {
    const cmeta_enum_desc *meta;
} cmeta_data_enum_shape;

typedef struct cmeta_data_field_desc {
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_field_desc;

typedef struct cmeta_data_struct_shape {
    const cmeta_struct_desc *layout;
    const cmeta_data_field_desc *fields;
    size_t field_count;
} cmeta_data_struct_shape;

typedef struct cmeta_data_variant_case {
    int64_t tag;
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_variant_case;

typedef struct cmeta_data_variant_shape {
    size_t tag_offset;
    const cmeta_data_desc *tag;
    const cmeta_data_variant_case *cases;
    size_t case_count;
} cmeta_data_variant_shape;
```

Public helpers:

```c
bool cmeta_data_kind_valid(cmeta_data_kind kind);
bool cmeta_data_kind_is_container(cmeta_data_kind kind);
bool cmeta_data_desc_valid(const cmeta_data_desc *desc);

const cmeta_data_field_desc *cmeta_data_struct_field(
    const cmeta_data_struct_shape *shape, size_t index);
const cmeta_data_field_desc *cmeta_data_struct_find_field(
    const cmeta_data_struct_shape *shape, const char *name);
const cmeta_data_variant_case *cmeta_data_variant_case_by_tag(
    const cmeta_data_variant_shape *shape, int64_t tag);
```

Built-in declarations:

```c
extern const cmeta_data_desc cmeta_data_bool;
extern const cmeta_data_desc cmeta_data_int;
extern const cmeta_data_desc cmeta_data_long;
extern const cmeta_data_desc cmeta_data_size;
extern const cmeta_data_desc cmeta_data_float;
extern const cmeta_data_desc cmeta_data_double;

extern const cmeta_data_desc cmeta_data_sequence;
extern const cmeta_data_desc cmeta_data_set;
extern const cmeta_data_desc cmeta_data_map;
```

- [ ] **Step 1: Add the failing independent-header test**

Create `cmeta/tests/cmeta_data_test.c`:

```c
#include <cmeta/data.h>
#include "tinytest.h"

spec("CMeta semantic data descriptors") {
    it("exposes primitive semantic descriptors") {
        check_equal(cmeta_data_bool.kind, CMETA_DATA_BOOL);
        check_equal(cmeta_data_int.kind, CMETA_DATA_SINT);
        check_equal(cmeta_data_size.kind, CMETA_DATA_UINT);
        check_equal(cmeta_data_float.kind, CMETA_DATA_FLOAT);
        check_true(cmeta_data_desc_valid(&cmeta_data_bool));
    }

    it("keeps container categories free of T K V") {
        check_equal(cmeta_data_sequence.kind, CMETA_DATA_SEQUENCE);
        check_equal(cmeta_data_set.kind, CMETA_DATA_SET);
        check_equal(cmeta_data_map.kind, CMETA_DATA_MAP);
        check_null(cmeta_data_sequence.storage_type);
        check_null(cmeta_data_sequence.shape);
    }
}
```

- [ ] **Step 2: Register the target and verify RED**

Add `cmeta_data_test` to `cmeta/tests/CMakeLists.txt`, linked to `Salts::CMeta` and `Salts::TinyTest`, C11/no extensions, and include it in the existing `-Werror=missing-field-initializers` group.

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --target cmeta_data_test
```

Expected: compile failure because `<cmeta/data.h>` does not exist.

- [ ] **Step 3: Create `data.h` with the exact API above**

Make it independently includable from C11/C++17:

```c
#include <cmeta/cmeta.h>
#include <cmeta/struct.h>
```

Do not include CSerde/CBind/Container headers.

- [ ] **Step 4: Re-export semantic metadata from `cmeta/meta.h`**

Add outside the C++ guard:

```c
#include <cmeta/data.h>
```

- [ ] **Step 5: Verify the intended link-level RED boundary**

```bash
cmake --build --preset linux-release-user --target cmeta_data_test
```

Expected: compile succeeds but link fails because built-ins/helpers are declared and not implemented yet.

- [ ] **Step 6: Commit the contract/test boundary**

```bash
git add \
  cmeta/include/cmeta/data.h \
  cmeta/include/cmeta/meta.h \
  cmeta/tests/cmeta_data_test.c \
  cmeta/tests/CMakeLists.txt
git commit -m "test(cmeta): define semantic data descriptor contract"
```

## Task 3: Implement shallow validation and immutable built-ins

**Files:**
- Create: `cmeta/src/data.c`
- Modify: `cmeta/CMakeLists.txt`
- Modify: `cmeta/tests/cmeta_data_test.c`

**Interfaces:**
- Consumes: Task 2 API.
- Produces: built-ins, shallow validation, struct/variant lookup.

Validation is deliberately shallow: direct invariants are checked, but field/case `value` descriptors are not recursively validated. This permits recursive semantic graphs.

- [ ] **Step 1: Add scalar-shape and append-safe-size tests**

Add a malformed integer descriptor:

```c
cmeta_data_integer_shape bad_int = { .bits = 7u };
cmeta_data_desc bad = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.bad-int",
    .display_name = "bad int",
    .kind = CMETA_DATA_SINT,
    .storage_type = &cmeta_type_int,
    .shape = &bad_int
};
check_false(cmeta_data_desc_valid(&bad));
```

Then create a valid copy of `cmeta_data_int` whose reported size is exactly the v1 prefix through `shape`:

```c
cmeta_data_desc prefix = cmeta_data_int;
prefix.struct_size =
    offsetof(cmeta_data_desc, shape) + sizeof(prefix.shape);
check_true(cmeta_data_desc_valid(&prefix));
```

This expression is the v1 ABI minimum. The implementation must not compare against `sizeof(cmeta_data_desc)`.

Require integer widths in `{8,16,32,64}`, float widths in `{32,64}`, and valid buffer ownership enum values.

- [ ] **Step 2: Add struct lookup/layout-consistency tests**

Define:

```c
Struct(cmeta_data_test_record,
    (int, id),
    (long, score)
);
```

Create semantic fields pointing to `cmeta_data_int` and `cmeta_data_long`. Require:

```c
check_true(cmeta_data_struct_field(&shape, 0u) == &fields[0]);
check_true(cmeta_data_struct_find_field(&shape, "score") == &fields[1]);
check_null(cmeta_data_struct_find_field(&shape, "missing"));
```

A STRUCT descriptor is valid only when every semantic field has non-empty stable ID/name, non-NULL value descriptor, and matches a reflected layout field with the same name and offset. Semantic fields may be a subset of layout fields.

- [ ] **Step 3: Add variant lookup and duplicate-tag tests**

Build a synthetic two-case variant with tags 1 and 2. Require lookup by both tags and NULL for an unknown tag. A duplicate tag makes the variant invalid. The tag semantic kind must be `SINT`, `UINT`, or `ENUM`.

Because the v1 case-tag ABI is `int64_t`, a `UINT` tag kind does not admit the
full `uint64_t` domain. Require `INT64_MAX` to remain valid and require binding
or schema adaptation to reject any unsigned discriminator above `INT64_MAX`
before lookup; do not cast or truncate it into `int64_t`.

- [ ] **Step 4: Implement immutable built-ins in `cmeta/src/data.c`**

Use `CHAR_BIT`:

```c
static const cmeta_data_integer_shape cmeta_data_int_shape = {
    (uint8_t)(sizeof(int) * CHAR_BIT)
};
```

Define:

```text
cmeta_data_bool    -> stable_id cmeta.bool.data,   BOOL,  &cmeta_type_bool
cmeta_data_int     -> stable_id cmeta.int.data,    SINT,  &cmeta_type_int
cmeta_data_long    -> stable_id cmeta.long.data,   SINT,  &cmeta_type_long
cmeta_data_size    -> stable_id cmeta.size.data,   UINT,  &cmeta_type_size
cmeta_data_float   -> stable_id cmeta.float.data,  FLOAT, &cmeta_type_float
cmeta_data_double  -> stable_id cmeta.double.data, FLOAT, &cmeta_type_double
```

Define shared abstract container categories with no concrete type arguments:

```text
cmeta_data_sequence -> stable_id cmeta.data.sequence, kind SEQUENCE,
                       storage_type NULL, shape NULL
cmeta_data_set      -> stable_id cmeta.data.set,      kind SET,
                       storage_type NULL, shape NULL
cmeta_data_map      -> stable_id cmeta.data.map,      kind MAP,
                       storage_type NULL, shape NULL
```

- [ ] **Step 5: Implement `cmeta_data_desc_valid()` with prefix-size semantics**

Use a file-local field-end helper and require:

```c
desc->struct_size >=
    offsetof(cmeta_data_desc, shape) + sizeof(desc->shape)
```

Never require `struct_size >= sizeof(cmeta_data_desc)`.

Direct rules:

```text
all kinds:
  ABI version == 1
  non-empty stable_id/display_name
  valid kind

non-container kinds:
  storage_type != NULL and cmeta_type_desc_valid(storage_type)

BOOL:
  shape == NULL

SINT/UINT:
  integer shape exists; bits in {8,16,32,64}

FLOAT:
  float shape exists; bits in {32,64}

STRING/BYTES:
  buffer shape exists; ownership enum valid

ENUM:
  enum shape/meta exists; meta name non-empty;
  items non-NULL when count != 0

STRUCT:
  struct shape/layout exists;
  fields non-NULL when count != 0;
  every field matches reflected name+offset

VARIANT:
  variant shape/tag/cases valid;
  tag kind SINT/UINT/ENUM;
  cases have non-empty stable_id/name, non-NULL value;
  tags unique

SEQUENCE/SET/MAP:
  storage_type == NULL and shape == NULL

CUSTOM:
  storage_type valid and shape != NULL
```

Do not recursively call `cmeta_data_desc_valid()` on field/case values.

- [ ] **Step 6: Implement lookup helpers**

`cmeta_data_struct_field()` returns by index; `cmeta_data_struct_find_field()` compares semantic field names; `cmeta_data_variant_case_by_tag()` scans immutable cases by tag. All return NULL for invalid/null inputs.

- [ ] **Step 7: Add `data.c` to CMeta and verify GREEN**

Modify `cmeta/CMakeLists.txt`:

```cmake
add_library(${TARGET_NAME}
  src/cmeta.c
  src/cstl_type.c
  src/data.c
  src/entry.c
  src/type_identity.c)
```

Run:

```bash
cmake --build --preset linux-release-user --target \
  cmeta_data_test cmeta_header_cpp_test cmeta_meta_header_test
ctest --preset linux-release-user -R '^cmeta_' --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add \
  cmeta/src/data.c \
  cmeta/CMakeLists.txt \
  cmeta/tests/cmeta_data_test.c
git commit -m "feat(cmeta): add semantic data descriptors"
```

## Task 4: Lock C++17 semantic ABI coverage

**Files:**
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

- [ ] **Step 1: Add standard-layout and runtime checks**

Include `<cmeta/data.h>` and add:

```cpp
static_assert(std::is_standard_layout_v<cmeta_data_desc>);
static_assert(std::is_standard_layout_v<cmeta_data_struct_shape>);
static_assert(std::is_standard_layout_v<cmeta_data_variant_shape>);

check_true(cmeta_data_desc_valid(&cmeta_data_int));
check_equal(cmeta_data_set.kind, CMETA_DATA_SET);
```

- [ ] **Step 2: Run the C++ target**

```bash
cmake --build --preset linux-release-user --target cmeta_header_cpp_test
ctest --preset linux-release-user -R '^cmeta_header_cpp_test$' \
  --output-on-failure
```

Expected: PASS under C++17 without C `_Generic` expansion.

- [ ] **Step 3: Commit**

```bash
git add cmeta/tests/cmeta_header_cpp_test.cpp
git commit -m "test(cmeta): cover semantic descriptors in C++"
```

### PR C1 verification gate

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^(cmeta_|cflow_|cstl_)' \
  --output-on-failure
git diff --check
```

Windows CI must pass. C1 must not modify Container production sources.

---

# PR C2 — Container semantic projection from proven TYPE applications

## Task 5: Append the semantic category tail to `cmeta_container_ext`

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/include/cmeta/data.h`
- Modify: `cmeta/src/data.c`
- Modify: `cmeta/tests/cmeta_container_type_test.c`
- Modify: `cmeta/tests/cmeta_data_test.c`

**Interfaces:**
- Consumes: C0 `cmeta_container_extension()`, C1 `cmeta_data_desc`, existing generic type helpers.
- Produces:

```c
struct cmeta_data_desc;

typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
    const struct cmeta_data_desc *data;
} cmeta_container_ext;

const cmeta_data_desc *cmeta_container_data(const void *object);
```

- [ ] **Step 1: Append `data` in the test build and prove the old prefix remains valid**

After adding the field declaration, create a synthetic legacy extension:

```c
cmeta_container_ext legacy_ext = cmeta_test_sequence_ext;
legacy_ext.struct_size =
    offsetof(cmeta_container_ext, type) + sizeof(legacy_ext.type);
legacy_ext.data = NULL;
```

Attach it to a local descriptor and require:

```c
check_not_null(cmeta_container_extension(&legacy_sequence));
check_true(cmeta_container_type_application_valid(&legacy_sequence));
```

This must pass even after `sizeof(cmeta_container_ext)` grows.

- [ ] **Step 2: Add the failing semantic-projection test**

Create a full-size synthetic extension:

```c
.data = &cmeta_data_sequence
```

Require:

```c
check_true(cmeta_container_data(&sequence) ==
           &cmeta_data_sequence);
```

For a synthetic element descriptor with `identity == NULL`, require
`cmeta_container_data()` to return the valid `.data` category while
`cmeta_container_type_application_valid()` returns false. This locks the
category/type separation at the CMeta boundary.

Expected before implementation: link failure on missing `cmeta_container_data()`.

- [ ] **Step 3: Append `data` without bumping ABI version**

Forward-declare `struct cmeta_data_desc` in `range.h` and append `data` after `type`. Keep `CMETA_CONTAINER_EXT_ABI_VERSION == 1`; an optional append-only tail is not an incompatible layout break.

- [ ] **Step 4: Implement `cmeta_container_data()`**

In `data.c`:

```text
1. reject NULL object
2. obtain cmeta_container_extension(object)
3. require struct_size through ext.data
4. require ext.data != NULL
5. require cmeta_data_desc_valid(ext.data)
6. return ext.data
```

Use:

```c
offsetof(cmeta_container_ext, data) + sizeof(ext->data)
```

before reading `ext->data`.

Do not return, copy, or cache T/K/V. Callers still use `cmeta_container_type_argument()`.

- [ ] **Step 5: Verify CMeta regression**

```bash
cmake --build --preset linux-release-user --target \
  cmeta_container_type_test cmeta_data_test
ctest --preset linux-release-user \
  -R '^cmeta_(cstl_type|data)_test$' --output-on-failure
```

Expected: PASS, including old-prefix compatibility.

- [ ] **Step 6: Commit**

```bash
git add \
  cmeta/include/cmeta/range.h \
  cmeta/include/cmeta/data.h \
  cmeta/src/data.c \
  cmeta/tests/cmeta_container_type_test.c \
  cmeta/tests/cmeta_data_test.c
git commit -m "feat(cmeta): project container semantic categories"
```

## Task 6: Attach Container canonical extensions to shared categories

**Files:**
- Modify: `cstl/src/generic_meta.c`
- Create: `cstl/tests/cstl_semantic_data_test.c`
- Modify: `cstl/tests/CMakeLists.txt`
- Modify: `cstl/tests/cstl_header_typed_cpp_test.cpp`

**Interfaces:**
- Consumes: canonical ext objects from `#37`, shared CMeta categories from C1, Task 5 projection helper.
- Produces exactly:

```text
typed(Vec, VecName, T)       -> SEQUENCE
typed(Deque, DequeName, T)   -> SEQUENCE
typed(List, ListName, T)     -> SEQUENCE
typed(Stack, StackName, T)   -> SEQUENCE
typed(Queue, QueueName, T)   -> SEQUENCE

typed(Set, SetName, T)           -> SET
typed(HashSet, HashSetName, T)   -> SET

typed(HashMap, HashMapName, K, V)     -> MAP
typed(Map, MapName, K, V)             -> MAP
typed(BTree, BTreeName, K, V)         -> MAP
typed(BPlusTree, BPlusTreeName, K, V) -> MAP

typed(Heap, HeapName, T)               -> unresolved / NULL
typed(MultiMap, MultiMapName, K, V)    -> unresolved / NULL
```

- [ ] **Step 1: Write the semantic mapping test before wiring `.data`**

Create `cstl/tests/cstl_semantic_data_test.c`:

```c
#include <cmeta/data.h>
#include <cstl/typed.h>
#include "tinytest.h"

spec("Container semantic data projection") {
    it("projects sequence containers without copying T") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);

        check_true(cmeta_container_data(&vec) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data(&deque) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data(&list) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data(&stack) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data(&queue) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_type_argument(&vec, 0u) ==
                   &cmeta_type_int);
    }
}
```

Add separate tests for Set/HashSet and the four MAP families.

- [ ] **Step 2: Add the semantic negative cases**

```c
Heap(int, heap);
MultiMap(int, int, multimap);
check_null(cmeta_container_data(&heap));
check_null(cmeta_container_data(&multimap));
```

Raw bytes:

```c
vec_t raw = { .cmeta = { &stl_vec_container_desc } };
check_equal(vec_init_bytes(&raw, sizeof(int), _Alignof(int), 2u), STL_OK);
check_true(cmeta_container_data(&raw) == &cmeta_data_sequence);
check_false(cmeta_container_type_application_valid(&raw));
vec_raw_destroy_storage(&raw);
```

This proves semantic category discovery does not invent `T`: `.data = SEQUENCE`
is visible, while the raw object still has no valid  Vec`<T>` application and
must be rejected by typed consumers.

- [ ] **Step 3: Register the new test and verify RED**

Add `cstl_semantic_data_test` to `cstl/tests/CMakeLists.txt`, linked to `Salts::CSTL`, `Salts::CMeta`, `Salts::TinyTest`, C11/no extensions.

```bash
cmake --build --preset linux-release-user --target cstl_semantic_data_test
ctest --preset linux-release-user -R '^cstl_semantic_data_test$' \
  --output-on-failure
```

Expected: runtime failure because Container extensions do not yet populate `.data`.

- [ ] **Step 4: Wire only `cstl/src/generic_meta.c`**

Change its extension initializer macros to designated initializers:

```c
const cmeta_container_ext stl_##prefix##_container_ext = {
    .struct_size = sizeof(cmeta_container_ext),
    .abi_version = CMETA_CONTAINER_EXT_ABI_VERSION,
    .type = &stl_##prefix##_generic_type_ops,
    .data = (semantic_desc)
};
```

Pass `&cmeta_data_sequence`, `&cmeta_data_set`, `&cmeta_data_map`, or `NULL` according to the exact mapping above.

Do not infer semantics from constructor name strings. Do not modify `instance_meta.c`, `list.c`, `map.c`, or `associative_meta.c`; they already reference the canonical ext objects.

- [ ] **Step 5: Verify semantic + generic regressions**

```bash
cmake --build --preset linux-release-user --target \
  cstl_semantic_data_test cstl_generic_identity_test \
  cstl_header_typed_test
ctest --preset linux-release-user \
  -R '^cstl_(semantic_data|generic_identity|header_typed)_test$' \
  --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Add C++17 canonical-metadata coverage**

In `cstl/tests/cstl_header_typed_cpp_test.cpp`, include `<cmeta/data.h>` and require:

```cpp
check_true(stl_vec_container_ext.data == &cmeta_data_sequence);
check_true(stl_set_container_ext.data == &cmeta_data_set);
check_true(stl_map_container_ext.data == &cmeta_data_map);
check_true(stl_heap_container_ext.data == nullptr);
check_true(stl_multimap_container_ext.data == nullptr);
```

- [ ] **Step 7: Commit**

```bash
git add \
  cstl/src/generic_meta.c \
  cstl/tests/cstl_semantic_data_test.c \
  cstl/tests/CMakeLists.txt \
  cstl/tests/cstl_header_typed_cpp_test.cpp
git commit -m "feat(container): expose semantic container categories"
```

### PR C2 verification gate

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^(cmeta_|cflow_|cstl_)' \
  --output-on-failure
git diff --check
```

Before Ready, verify the final diff establishes all of these:

```text
cmeta_data_sequence/set/map contain no T/K/V
cmeta_container_type_argument() remains the sole concrete T/K/V API
raw-byte Vec projects a semantic category but remains an invalid typed application
Heap and MultiMap remain unresolved
only generic_meta.c changes in Container production code
no Range/Collector/container algorithm body changes
no CSerde/CBind/TurboParser/DataBind production code changes
```

Windows CI must pass on the exact final head.

---

# Self-review against the amended design

## Spec coverage

- Append-only `cmeta_container_ext`: C0 Task 1 and C2 Task 5 backward-prefix regression.
- Append-only `cmeta_data_desc`: C1 Task 3 prefix-size regression and validation rule.
- Versioned semantic core: C1 Tasks 2–4.
- Scalar/string/bytes/enum/struct/variant shapes: C1 Tasks 2–3.
- `SET`: C1 + C2.
- No semantic OPTIONAL: global constraint; no task introduces it.
- No duplicate T/K/V: shared semantic categories + existing type argument API only.
- Explicit Heap/MultiMap non-mapping: C2 Task 6.
- Raw-byte category/type separation contract: C2 Task 6.
- C11/C++17 compatibility: each PR has public-header coverage.
- Nested zero-field construction: explicitly deferred; no false completion claim.

## Type consistency

The implementation names are fixed throughout:

```text
cmeta_container_extension
cmeta_data_desc
cmeta_data_sequence
cmeta_data_set
cmeta_data_map
cmeta_container_data
```

No new constructor-equality public API is added because this design performs semantic projection through the explicit extension link and does not need a second constructor matcher.

## Placeholder scan

There are no TBD/TODO implementation steps. Deferred work has explicit boundaries and named follow-on plans.

---

# Next plan after C2

After C2 merges, create a separate construction/static-type-application plan for zero-state nested fields:

```text
static field TYPE application
        -> cmeta_container_bind_types(empty object, declared type)
        -> validated container TYPE application
        -> Collector transaction
```

Only after that construction contract is proven should CSerde/CBind implementation begin.
