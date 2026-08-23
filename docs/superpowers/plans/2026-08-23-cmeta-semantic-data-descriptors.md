# CMeta Semantic Data Descriptors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a versioned, format-neutral CMeta semantic descriptor layer and project already-proven TurboSTL generic applications into `SEQUENCE` / `SET` / `MAP` without storing a second copy of `T/K/V`.

**Architecture:** `TYPE<A...>` remains the only generic argument source. CMeta semantic descriptors describe meaning, not wire syntax and not generic arguments. TurboSTL container extensions link a valid generic application to one shared semantic category descriptor; concrete type arguments continue to come only from `cmeta_container_type_argument()`. Before appending semantic metadata to `cmeta_container_ext`, harden its `struct_size` handling to true append-only prefix semantics.

**Tech Stack:** C11, C++17 public-header compatibility, CMake presets, TinyTest, TurboUtils::CMeta, TurboUtils::STL.

**Spec:** `docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md`, `docs/superpowers/specs/2026-08-23-serialization-data-binding-generic-foundation-amendment.md`, and `docs/superpowers/specs/2026-08-23-serialization-data-binding-semantic-foundation-amendment.md`.

## Global Constraints

- Base implementation starts from `master` at or after merge commit `93e68ef443b86aa887cff21d2ceeb134ad32e0e4` (`#37`).
- `TYPE<A...>` owns generic constructor/arity/argument identity. Semantic container descriptors must not contain `element`, `key`, or `value` type fields.
- Constructor equality is by valid `stable_id`, never by pointer identity across translation units.
- Type well-formedness remains separate from operation capability/traits.
- `SET` is a first-class semantic kind.
- `OPTIONAL` is not part of this plan. It remains blocked until real `Option<T>` value-generic identity exists.
- Field presence (`required`, missing, default), aliases, external names and emit policy remain CBind/schema policy.
- No CSerde, CBind, parser, format codec, DataBind or TBE production code is introduced.
- No TurboSTL raw algorithm, ownership rule, Range traversal or Collector behavior is rewritten.
- Raw byte containers without valid CMeta type arguments must not resolve to semantic container descriptors.
- Heap and MultiMap remain semantically unresolved in v1; do not silently map Heap to SEQUENCE or MultiMap to MAP.
- This plan does not claim nested zero-initialized `vec_t/map_t` struct-field decode. Static field type application + `construct.bind_types` is the next plan.
- First semantic descriptor ABI does not reserve an unspecified `cmeta_data_ops` callback pointer. Access/lifecycle ops are introduced only when their contract is concrete.
- Semantic fingerprint generation is a later plan after descriptor graph semantics are stable.
- Public CMeta/TurboSTL headers must compile as C11 and C++17.
- Use repository presets. Linux verification uses `release-linux-ninja`, `build-default-linux`, and `test-release-linux`; Windows is required through the repository CI path.

---

## PR decomposition

Execute this plan as three independently reviewable implementation PRs:

```text
PR C0  CMeta generic equality + append-only container extension accessor
PR C1  CMeta semantic descriptor core
PR C2  TurboSTL semantic projection via cmeta_container_ext.data
```

PR C1 starts only after C0 merges. PR C2 starts only after C1 merges. Do not stack production PRs on unmerged heads.

---

# PR C0 — Generic equality and extension ABI hardening

## Task 1: Make generic constructor equality a public single rule

**Files:**
- Modify: `cmeta/include/cmeta/type_identity.h`
- Modify: `cmeta/src/type_identity.c`
- Modify: `cmeta/tests/cmeta_type_identity_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Consumes: `cmeta_generic_desc`, existing private stable-ID comparison inside `cmeta_type_identity_equal()`.
- Produces:

```c
bool cmeta_generic_desc_equal(
    const cmeta_generic_desc *a,
    const cmeta_generic_desc *b);
```

`cmeta_type_identity_equal()` must call this public function for `CMETA_TYPE_APPLY`.

- [ ] **Step 1: Write the failing direct constructor-equality test**

In `cmeta/tests/cmeta_type_identity_test.c`, add two independently allocated constructor descriptors with the same stable ID and one different constructor:

```c
static const cmeta_generic_desc test_seq_a =
    CMETA_GENERIC_DESC_INIT("test.Sequence", "Sequence", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);
static const cmeta_generic_desc test_seq_b =
    CMETA_GENERIC_DESC_INIT("test.Sequence", "Sequence peer", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);
static const cmeta_generic_desc test_set =
    CMETA_GENERIC_DESC_INIT("test.Set", "Set", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);
```

Add:

```c
it("compares generic constructors by stable identity") {
    check_true(&test_seq_a != &test_seq_b);
    check_true(cmeta_generic_desc_equal(&test_seq_a, &test_seq_b));
    check_false(cmeta_generic_desc_equal(&test_seq_a, &test_set));
    check_false(cmeta_generic_desc_equal(NULL, &test_seq_a));
}
```

- [ ] **Step 2: Run focused build and verify RED**

Run:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target cmeta_type_identity_test
```

Expected: compile failure because `cmeta_generic_desc_equal()` is not public yet.

- [ ] **Step 3: Promote the existing private helper to the public API**

Declare it in `cmeta/include/cmeta/type_identity.h` immediately after `cmeta_generic_desc_valid()`.

In `cmeta/src/type_identity.c`, remove `static` from the current helper and keep the stable-ID rule centralized there. Require both descriptors to be valid before comparing stable IDs:

```c
bool cmeta_generic_desc_equal(const cmeta_generic_desc *a,
                              const cmeta_generic_desc *b) {
    if (a == b) return a != NULL && cmeta_generic_desc_valid(a);
    return cmeta_generic_desc_valid(a) && cmeta_generic_desc_valid(b) &&
           strcmp(a->stable_id, b->stable_id) == 0;
}
```

Do not compare `display_name` or addresses. `cmeta_type_identity_equal()` must continue delegating APPLY constructor comparison to this function.

- [ ] **Step 4: Add C++17 direct API coverage**

In `cmeta/tests/cmeta_header_cpp_test.cpp`, construct two `cmeta_generic_desc` values with one stable ID and require `cmeta_generic_desc_equal()` to return true.

- [ ] **Step 5: Verify GREEN**

Run:

```bash
cmake --build --preset build-default-linux --target \
  cmeta_type_identity_test cmeta_header_cpp_test
ctest --preset test-release-linux -R '^cmeta_(type_identity|header_cpp)_test$' \
  --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 6: Commit**

```bash
git add \
  cmeta/include/cmeta/type_identity.h \
  cmeta/src/type_identity.c \
  cmeta/tests/cmeta_type_identity_test.c \
  cmeta/tests/cmeta_header_cpp_test.cpp
git commit -m "feat(cmeta): expose generic constructor equality"
```

## Task 2: Add a prefix-safe public container-extension accessor

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/src/container_type.c`
- Modify: `cmeta/tests/cmeta_container_type_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Consumes: `cmeta_container_desc.ext`, `CMETA_CONTAINER_EXT_ABI_VERSION`, `CMETA_CONTAINER_TYPE_OPS_ABI_VERSION`.
- Produces:

```c
const cmeta_container_ext *cmeta_container_extension(const void *object);
```

The accessor validates only the currently required prefix through `ext.type`; it must not require `struct_size >= sizeof(cmeta_container_ext)`.

- [ ] **Step 1: Write the failing accessor test**

In `cmeta/tests/cmeta_container_type_test.c`, reuse the existing synthetic sequence object and add:

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

- [ ] **Step 2: Verify RED**

Run:

```bash
cmake --build --preset build-default-linux --target cmeta_container_type_test
```

Expected: compile failure because `cmeta_container_extension()` does not exist.

- [ ] **Step 3: Declare the accessor and implement field-end prefix validation**

Declare the function in `cmeta/include/cmeta/range.h` before the existing container type helpers.

In `cmeta/src/container_type.c`, use a field-end calculation instead of `sizeof(*ext)`:

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

Refactor `cmeta_container_type_ops_of()` to use this accessor.

Likewise validate `cmeta_container_type_ops` through its current last required member `argument`, not `sizeof(*ops)`:

```c
ops->struct_size >= CMETA_FIELD_END(cmeta_container_type_ops, argument)
```

This is required so future optional tail fields do not invalidate v1 prefixes.

- [ ] **Step 4: Add a reported-prefix-size regression**

Copy the existing synthetic extension to a local mutable value and set:

```c
ext.struct_size = offsetof(cmeta_container_ext, type) + sizeof(ext.type);
```

Attach it to a local descriptor and verify both:

```c
check_not_null(cmeta_container_extension(&sequence));
check_true(cmeta_container_type_application_valid(&sequence));
```

This locks the append-only rule before C2 adds the semantic tail.

- [ ] **Step 5: Add C++17 surface coverage**

In `cmeta/tests/cmeta_header_cpp_test.cpp`, include `<cmeta/range.h>` and assert that the extension/type-ops structs remain standard layout; construct an extension with `.struct_size` equal to the prefix through `type` and verify the API compiles under C++17.

- [ ] **Step 6: Run CMeta regression**

Run:

```bash
cmake --build --preset build-default-linux --target \
  cmeta_container_type_test cmeta_type_identity_test cmeta_header_cpp_test
ctest --preset test-release-linux -R '^cmeta_' --output-on-failure
```

Expected: all selected CMeta tests pass.

- [ ] **Step 7: Commit**

```bash
git add \
  cmeta/include/cmeta/range.h \
  cmeta/src/container_type.c \
  cmeta/tests/cmeta_container_type_test.c \
  cmeta/tests/cmeta_header_cpp_test.cpp
git commit -m "refactor(cmeta): make container extensions append-safe"
```

### PR C0 verification gate

Run on a fresh tree:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux -R '^(cmeta_|cflow_|turbostl_)' \
  --output-on-failure
git diff --check
```

The PR must also pass the repository Windows CI path. No semantic descriptor or TurboSTL production file changes belong in C0.

---

# PR C1 — CMeta semantic descriptor core

## Task 3: Define the versioned semantic descriptor public API

**Files:**
- Create: `cmeta/include/cmeta/data.h`
- Create: `cmeta/tests/cmeta_data_test.c`
- Modify: `cmeta/include/cmeta/meta.h`
- Modify: `cmeta/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cmeta_type_desc`, `cmeta_enum_desc`, `cmeta_struct_desc`.
- Produces the first semantic descriptor ABI.

The exact public kinds are:

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
```

There is deliberately no `CMETA_DATA_OPTIONAL` in this PR.

Descriptor:

```c
enum {
    CMETA_DATA_DESC_ABI_VERSION = 1u
};

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

Kind-specific shapes:

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
```

Struct and variant:

```c
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

Public helpers declared in this task:

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

Built-in descriptor declarations:

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

- [ ] **Step 1: Write a test that includes only `<cmeta/data.h>`**

Create `cmeta/tests/cmeta_data_test.c` and initially require the new API to exist:

```c
#include <cmeta/data.h>
#include "tinytest.h"

spec("CMeta semantic data descriptors") {
    it("exposes versioned primitive semantic descriptors") {
        check_equal(cmeta_data_bool.kind, CMETA_DATA_BOOL);
        check_equal(cmeta_data_int.kind, CMETA_DATA_SINT);
        check_equal(cmeta_data_size.kind, CMETA_DATA_UINT);
        check_equal(cmeta_data_float.kind, CMETA_DATA_FLOAT);
        check_true(cmeta_data_desc_valid(&cmeta_data_bool));
        check_true(cmeta_data_desc_valid(&cmeta_data_int));
    }

    it("keeps semantic container categories free of concrete type arguments") {
        check_equal(cmeta_data_sequence.kind, CMETA_DATA_SEQUENCE);
        check_equal(cmeta_data_set.kind, CMETA_DATA_SET);
        check_equal(cmeta_data_map.kind, CMETA_DATA_MAP);
        check_null(cmeta_data_sequence.storage_type);
        check_null(cmeta_data_sequence.shape);
    }
}
```

- [ ] **Step 2: Register the new test target and verify RED**

In `cmeta/tests/CMakeLists.txt` add `cmeta_data_test` linked to `TurboUtils::CMeta` and `TurboUtils::TinyTest`, C11, no extensions, and include it in the existing `-Werror=missing-field-initializers` group.

Run:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target cmeta_data_test
```

Expected: compile failure because `<cmeta/data.h>` does not exist.

- [ ] **Step 3: Create `data.h` with the exact v1 API above**

`data.h` must be independently includable from C11 and C++17. Include only what is needed:

```c
#include <cmeta/cmeta.h>
#include <cmeta/struct.h>
```

Do not include CSerde/CBind/TurboSTL headers. Do not add format-specific fields or an unspecified callback table.

- [ ] **Step 4: Re-export semantic metadata through `cmeta/meta.h`**

Add:

```c
#include <cmeta/data.h>
```

outside the `#ifndef __cplusplus` block so the semantic descriptor surface is available in C++17 too.

- [ ] **Step 5: Commit the API/test RED boundary**

After the header exists but before `data.c`, the test should link-fail on the declared built-in descriptors/helpers. Commit the public surface and tests as one reviewable step:

```bash
git add \
  cmeta/include/cmeta/data.h \
  cmeta/include/cmeta/meta.h \
  cmeta/tests/cmeta_data_test.c \
  cmeta/tests/CMakeLists.txt
git commit -m "test(cmeta): define semantic data descriptor contract"
```

## Task 4: Implement shallow semantic validation and built-in descriptors

**Files:**
- Create: `cmeta/src/data.c`
- Modify: `cmeta/CMakeLists.txt`
- Modify: `cmeta/tests/cmeta_data_test.c`

**Interfaces:**
- Consumes: Task 3 API.
- Produces: immutable built-ins, shallow descriptor validation, struct/variant lookup.

Validation is intentionally **shallow**: it validates direct descriptor/shape invariants but does not recursively call `cmeta_data_desc_valid()` through field/case values. This permits recursive semantic graphs and avoids recursion cycles in the validator.

- [ ] **Step 1: Extend the test with scalar shape validation**

Add cases such as:

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

Require integer widths to be 8/16/32/64 and float widths to be 32/64. Buffer ownership enum values must be valid.

- [ ] **Step 2: Add struct semantic lookup tests**

Define a reflected layout:

```c
Struct(cmeta_data_test_record,
    (int, id),
    (long, score)
);
```

Create two semantic fields pointing at `cmeta_data_int` / `cmeta_data_long`; verify:

```c
check_true(cmeta_data_struct_field(&shape, 0u) == &fields[0]);
check_true(cmeta_data_struct_find_field(&shape, "score") == &fields[1]);
check_null(cmeta_data_struct_find_field(&shape, "missing"));
```

`cmeta_data_desc_valid()` for `CMETA_DATA_STRUCT` must require a valid layout pointer and every semantic field to name an existing layout field at the same offset. Semantic fields may be a subset of layout fields.

- [ ] **Step 3: Add variant lookup/duplicate-tag tests**

Build a synthetic two-case variant descriptor with tags 1 and 2. Verify `cmeta_data_variant_case_by_tag()` finds both and returns NULL for an unknown tag.

A variant descriptor with duplicate tags must be invalid.

The variant tag semantic descriptor must be one of:

```text
SINT / UINT / ENUM
```

- [ ] **Step 4: Implement immutable built-ins in `cmeta/src/data.c`**

Use `CHAR_BIT` for platform-correct primitive widths:

```c
static const cmeta_data_integer_shape cmeta_data_int_shape = {
    (uint8_t)(sizeof(int) * CHAR_BIT)
};
```

Define:

```text
cmeta_data_bool    -> cmeta.bool.data / BOOL / &cmeta_type_bool
cmeta_data_int     -> cmeta.int.data / SINT / &cmeta_type_int
cmeta_data_long    -> cmeta.long.data / SINT / &cmeta_type_long
cmeta_data_size    -> cmeta.size.data / UINT / &cmeta_type_size
cmeta_data_float   -> cmeta.float.data / FLOAT / &cmeta_type_float
cmeta_data_double  -> cmeta.double.data / FLOAT / &cmeta_type_double
```

Use shared abstract category descriptors:

```text
cmeta_data_sequence -> stable_id "cmeta.data.sequence", kind SEQUENCE
cmeta_data_set      -> stable_id "cmeta.data.set",      kind SET
cmeta_data_map      -> stable_id "cmeta.data.map",      kind MAP
```

These three have `storage_type = NULL` and `shape = NULL`; they deliberately contain no T/K/V.

- [ ] **Step 5: Implement shallow `cmeta_data_desc_valid()`**

Rules:

```text
all kinds:
  struct_size >= sizeof(cmeta_data_desc)
  abi_version == 1
  non-empty stable_id/display_name
  valid kind
  if storage_type != NULL -> cmeta_type_desc_valid(storage_type)

BOOL:
  shape == NULL

SINT/UINT:
  integer shape exists; bits in {8,16,32,64}

FLOAT:
  float shape exists; bits in {32,64}

STRING/BYTES:
  buffer shape exists; ownership enum valid

ENUM:
  enum shape/meta exists; meta name non-empty; items non-NULL when count != 0

STRUCT:
  struct shape/layout exists; fields non-NULL when count != 0;
  each field has non-empty stable_id/name, value != NULL,
  matching reflected layout field and exact offset

VARIANT:
  variant shape/tag/cases valid; tag kind is SINT/UINT/ENUM;
  case stable_id/name/value non-NULL; tags unique

SEQUENCE/SET/MAP:
  storage_type == NULL and shape == NULL for the shared abstract category descriptors

CUSTOM:
  shape != NULL
```

Do not recursively validate field/case `value` descriptors.

- [ ] **Step 6: Add `data.c` to `TurboUtils::CMeta` and run focused tests**

Modify `cmeta/CMakeLists.txt`:

```cmake
add_library(${TARGET_NAME}
  src/cmeta.c
  src/container_type.c
  src/data.c
  src/entry.c
  src/type_identity.c)
```

Run:

```bash
cmake --build --preset build-default-linux --target \
  cmeta_data_test cmeta_header_cpp_test cmeta_meta_header_test
ctest --preset test-release-linux -R '^cmeta_' --output-on-failure
```

Expected: all CMeta tests pass.

- [ ] **Step 7: Commit implementation**

```bash
git add \
  cmeta/src/data.c \
  cmeta/CMakeLists.txt \
  cmeta/tests/cmeta_data_test.c
git commit -m "feat(cmeta): add semantic data descriptors"
```

## Task 5: Lock C++17 public-header compatibility

**Files:**
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Consumes: `<cmeta/data.h>` and Task 4 implementation.
- Produces: C++17 compile/runtime regression for the semantic descriptor ABI.

- [ ] **Step 1: Add C++17 descriptor checks**

Include `<cmeta/data.h>` and add:

```cpp
static_assert(std::is_standard_layout_v<cmeta_data_desc>);
static_assert(std::is_standard_layout_v<cmeta_data_struct_shape>);
static_assert(std::is_standard_layout_v<cmeta_data_variant_shape>);
```

At runtime verify:

```cpp
check_true(cmeta_data_desc_valid(&cmeta_data_int));
check_equal(cmeta_data_set.kind, CMETA_DATA_SET);
```

- [ ] **Step 2: Build and run the C++ test**

Run:

```bash
cmake --build --preset build-default-linux --target cmeta_header_cpp_test
ctest --preset test-release-linux -R '^cmeta_header_cpp_test$' \
  --output-on-failure
```

Expected: PASS under C++17 without `_Generic` use.

- [ ] **Step 3: Commit**

```bash
git add cmeta/tests/cmeta_header_cpp_test.cpp
git commit -m "test(cmeta): cover semantic descriptors in C++"
```

### PR C1 verification gate

Run fresh:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux -R '^(cmeta_|cflow_|turbostl_)' \
  --output-on-failure
git diff --check
```

Windows repository CI must pass. PR C1 must not modify TurboSTL production sources.

---

# PR C2 — TurboSTL semantic projection from proven generic applications

## Task 6: Append semantic category metadata to the versioned container extension

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Modify: `cmeta/include/cmeta/data.h`
- Modify: `cmeta/src/data.c`
- Modify: `cmeta/tests/cmeta_container_type_test.c`
- Modify: `cmeta/tests/cmeta_data_test.c`

**Interfaces:**
- Consumes: C0 `cmeta_container_extension()`, C1 `cmeta_data_desc`, and existing generic type helpers.
- Produces one append-only optional extension tail:

```c
struct cmeta_data_desc;

typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
    const struct cmeta_data_desc *data;
} cmeta_container_ext;
```

And one public semantic projection helper:

```c
const cmeta_data_desc *cmeta_container_data_descriptor(const void *object);
```

- [ ] **Step 1: Write the backward-prefix RED test before changing the validator**

After appending `data` to the struct declaration, modify the synthetic extension test so one valid extension reports only the old prefix size:

```c
legacy_ext.struct_size =
    offsetof(cmeta_container_ext, type) + sizeof(legacy_ext.type);
legacy_ext.data = NULL;
```

Require:

```c
check_not_null(cmeta_container_extension(&legacy_sequence));
check_true(cmeta_container_type_application_valid(&legacy_sequence));
```

If any code regresses to `struct_size >= sizeof(cmeta_container_ext)`, this test fails. This is the concrete proof that C0's prefix rule survives the first real tail append.

- [ ] **Step 2: Add the failing semantic projection test**

Create a full-size synthetic extension with:

```c
.data = &cmeta_data_sequence
```

Require:

```c
check_true(cmeta_container_data_descriptor(&sequence) ==
           &cmeta_data_sequence);
```

Also set the synthetic element type to a descriptor with `identity == NULL` and require semantic projection to return NULL even though `.data` exists.

Expected before implementation: link failure on missing `cmeta_container_data_descriptor()`.

- [ ] **Step 3: Append `data` and keep ABI version 1**

In `range.h`, forward-declare:

```c
struct cmeta_data_desc;
```

Append the `data` pointer after `type`. Do not bump `CMETA_CONTAINER_EXT_ABI_VERSION` for this append-only optional tail.

Existing extension producers that only report bytes through `.type` remain valid for generic type introspection.

- [ ] **Step 4: Implement semantic projection in `data.c`**

`cmeta_container_data_descriptor()` must:

```text
1. reject NULL object
2. require cmeta_container_type_application_valid(object)
3. obtain cmeta_container_extension(object)
4. require ext.struct_size to cover ext.data
5. require ext.data != NULL
6. require cmeta_data_desc_valid(ext.data)
7. require ext.data.kind is SEQUENCE/SET/MAP
8. return ext.data
```

Use `offsetof(cmeta_container_ext, data) + sizeof(ext->data)` for the tail-size check.

Do **not** return or cache T/K/V. Callers continue to use `cmeta_container_type_argument()`.

- [ ] **Step 5: Run CMeta focused regression**

Run:

```bash
cmake --build --preset build-default-linux --target \
  cmeta_container_type_test cmeta_data_test
ctest --preset test-release-linux -R '^cmeta_(container_type|data)_test$' \
  --output-on-failure
```

Expected: PASS, including the legacy-prefix regression.

- [ ] **Step 6: Commit CMeta extension tail**

```bash
git add \
  cmeta/include/cmeta/range.h \
  cmeta/include/cmeta/data.h \
  cmeta/src/data.c \
  cmeta/tests/cmeta_container_type_test.c \
  cmeta/tests/cmeta_data_test.c
git commit -m "feat(cmeta): project container semantic categories"
```

## Task 7: Attach TurboSTL constructors to shared semantic categories

**Files:**
- Modify: `turbostl/src/generic_meta.c`
- Create: `turbostl/tests/turbostl_semantic_data_test.c`
- Modify: `turbostl/tests/CMakeLists.txt`
- Modify: `turbostl/tests/turbostl_header_typed_cpp_test.cpp`

**Interfaces:**
- Consumes: canonical constructors/ext objects from `#37`, shared CMeta semantic category descriptors from C1, and `cmeta_container_data_descriptor()` from Task 6.
- Produces this exact v1 mapping:

```text
Vec<T>         -> cmeta_data_sequence
Deque<T>       -> cmeta_data_sequence
List<T>        -> cmeta_data_sequence
Stack<T>       -> cmeta_data_sequence
Queue<T>       -> cmeta_data_sequence

Set<T>         -> cmeta_data_set
HashSet<T>     -> cmeta_data_set

HashMap<K,V>   -> cmeta_data_map
Map<K,V>       -> cmeta_data_map
BTree<K,V>     -> cmeta_data_map
BPlusTree<K,V> -> cmeta_data_map

Heap<T>        -> NULL
MultiMap<K,V>  -> NULL
```

- [ ] **Step 1: Write the TurboSTL semantic mapping test before wiring `.data`**

Create `turbostl/tests/turbostl_semantic_data_test.c`:

```c
#include <cmeta/data.h>
#include <turbostl/typed.h>
#include "tinytest.h"

spec("TurboSTL semantic data projection") {
    it("projects ordered sequence containers without copying T") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);

        check_true(cmeta_container_data_descriptor(&vec) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&deque) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&list) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&stack) ==
                   &cmeta_data_sequence);
        check_true(cmeta_container_data_descriptor(&queue) ==
                   &cmeta_data_sequence);

        check_true(cmeta_container_type_argument(&vec, 0u) ==
                   &cmeta_type_int);
    }
}
```

Add separate tests for Set/HashSet and Map families.

- [ ] **Step 2: Add negative semantic-boundary tests**

Require:

```c
Heap(int, heap);
MultiMap(int, int, multimap);
check_null(cmeta_container_data_descriptor(&heap));
check_null(cmeta_container_data_descriptor(&multimap));
```

For raw storage:

```c
vec_t raw = { .cmeta = { &stl_vec_container_desc } };
check_equal(vec_init_bytes(&raw, sizeof(int), _Alignof(int), 2u), STL_OK);
check_null(cmeta_container_data_descriptor(&raw));
vec_raw_destroy_storage(&raw);
```

This proves a semantic category pointer on the canonical Vec extension is insufficient without a valid `Vec<T>` application.

- [ ] **Step 3: Register the new test and verify RED**

Add `turbostl_semantic_data_test` to `turbostl/tests/CMakeLists.txt`, linked to `TurboUtils::STL`, `TurboUtils::CMeta`, and `TurboUtils::TinyTest`, C11/no extensions.

Run:

```bash
cmake --build --preset build-default-linux --target turbostl_semantic_data_test
ctest --preset test-release-linux -R '^turbostl_semantic_data_test$' \
  --output-on-failure
```

Expected: runtime test failure because the current TurboSTL extensions do not populate `.data`.

- [ ] **Step 4: Change `generic_meta.c` extension initializers to designated initializers and assign semantic categories**

Update the generic metadata macros so extension objects use:

```c
const cmeta_container_ext stl_##prefix##_container_ext = {
    .struct_size = sizeof(cmeta_container_ext),
    .abi_version = CMETA_CONTAINER_EXT_ABI_VERSION,
    .type = &stl_##prefix##_generic_type_ops,
    .data = (semantic_desc)
};
```

Pass the semantic pointer explicitly per constructor. Do not derive meaning from constructor name strings at runtime.

Use the exact mapping above. Heap and MultiMap pass `NULL`.

This file is the only TurboSTL production implementation that needs semantic mapping changes. `instance_meta.c`, `list.c`, `map.c`, and `associative_meta.c` already point to the canonical ext objects and must not change.

- [ ] **Step 5: Run the semantic and existing generic tests**

Run:

```bash
cmake --build --preset build-default-linux --target \
  turbostl_semantic_data_test turbostl_generic_identity_test \
  turbostl_header_typed_test
ctest --preset test-release-linux \
  -R '^turbostl_(semantic_data|generic_identity|header_typed)_test$' \
  --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Add C++17 metadata coverage**

In `turbostl/tests/turbostl_header_typed_cpp_test.cpp`, include `<cmeta/data.h>` and verify canonical metadata is readable without expanding C-only declaration macros:

```cpp
check_true(stl_vec_container_ext.data == &cmeta_data_sequence);
check_true(stl_set_container_ext.data == &cmeta_data_set);
check_true(stl_map_container_ext.data == &cmeta_data_map);
check_true(stl_heap_container_ext.data == nullptr);
check_true(stl_multimap_container_ext.data == nullptr);
```

- [ ] **Step 7: Commit TurboSTL projection**

```bash
git add \
  turbostl/src/generic_meta.c \
  turbostl/tests/turbostl_semantic_data_test.c \
  turbostl/tests/CMakeLists.txt \
  turbostl/tests/turbostl_header_typed_cpp_test.cpp
git commit -m "feat(turbostl): expose semantic container categories"
```

### PR C2 verification gate

Run fresh Linux:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux -R '^(cmeta_|cflow_|turbostl_)' \
  --output-on-failure
git diff --check
```

Required review assertions before Ready:

```text
- cmeta_data_sequence/set/map have no T/K/V fields
- cmeta_container_type_argument remains the only concrete generic argument API
- raw byte Vec does not resolve semantically
- Heap and MultiMap remain NULL/unresolved
- no Range/Collector/container algorithm body changed
- no CSerde/CBind/TurboParser/DataBind code changed
```

Windows repository CI must pass on the exact final head.

---

# Self-review against the amended design

## Spec coverage

- Generic constructor equality: PR C0 Task 1.
- True append-only `struct_size` semantics before extension growth: PR C0 Task 2 and PR C2 Task 6 regression.
- Versioned semantic descriptor ABI: PR C1 Task 3.
- Scalar/string/bytes/enum/struct/variant semantic shapes: PR C1 Tasks 3–4.
- `SET` as distinct semantic kind: PR C1 + C2.
- No semantic OPTIONAL until `Option<T>` identity exists: global constraint; no task introduces it.
- No duplicated container T/K/V: shared category descriptors + existing type argument helpers only.
- Explicit Heap/MultiMap non-mapping: PR C2 Task 7.
- Raw byte negative contract: PR C2 Task 7.
- C11/C++17 surface: every PR includes C/C++ verification.
- Nested erased-field construction: explicitly deferred to the next plan; this plan does not claim that capability.

## Type consistency

The same names are used throughout:

```text
cmeta_generic_desc_equal
cmeta_container_extension
cmeta_data_desc
cmeta_data_sequence
cmeta_data_set
cmeta_data_map
cmeta_container_data_descriptor
```

No semantic container argument accessor is introduced; `cmeta_container_type_argument()` remains the sole concrete T/K/V source.

## Placeholder scan

This plan contains no TBD/TODO implementation steps. Deferred capabilities are explicit non-goals with a named next plan boundary rather than incomplete steps.

---

# Next plan after C2

After C2 merges, write a separate construction/type-application plan for zero-state nested fields. Its contract must build on the same versioned extension root and must not reopen semantic T/K/V duplication:

```text
static field TYPE application
        -> construct.bind_types(empty object, args...)
        -> validated container TYPE application
        -> Collector transaction
```

Only after that construction contract is proven should CSerde/CBind implementation begin.
