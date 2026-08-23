# CMeta Semantic Data Descriptors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce the first versioned, format-neutral CMeta semantic data descriptor surface, including record and variant shape metadata, without changing existing `Struct(...)`, `Enum(...)`, `cmeta_type_traits`, parser, or container behavior.

**Architecture:** Add a standalone public `cmeta/data.h` descriptor graph that distinguishes semantic meaning from C layout reflection. `cmeta/data.h` remains independently includable through forward declarations; `cmeta/cmeta.h` re-exports it only after `cmeta_type_desc` is defined. A small `data.c` owns shallow descriptor validation and lookup helpers; recursive fingerprinting, binding policy, lifecycle operations, CSerde, CBind, and container construction are later plans.

**Tech Stack:** C11, C++17 public-header compatibility, CMake presets, TinyTest, TurboUtils::CMeta.

**Spec:** `docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md`

## Global Constraints

- `Struct(...)` remains C ABI/layout reflection only; do not add serialization meaning to `cmeta_field_desc` or `cmeta_struct_desc`.
- `Enum(...)` remains the existing enum reflection contract; semantic enum descriptors reference `cmeta_enum_desc` rather than duplicating enum items.
- Do not add serialization callbacks to `cmeta_type_traits`.
- CMeta must not depend on TurboParser, CSerde, CBind, TurboSTL, or CFlow for this descriptor layer.
- Every public top-level semantic descriptor starts with `struct_size` and `abi_version`; v1 ABI constant is `CMETA_DATA_DESC_ABI_VERSION == 1`.
- The core kind universe in this plan includes `BOOL`, `SINT`, `UINT`, `FLOAT`, `STRING`, `BYTES`, `ENUM`, `STRUCT`, `VARIANT`, `OPTIONAL`, `SEQUENCE`, `MAP`, and `CUSTOM`.
- `VARIANT` is a first-class core kind; do not postpone it to a TBE-only type.
- `stable_id` is logical identity only. Do not implement semantic/schema fingerprint hashing in this plan; that is a later implementation plan from the approved spec.
- Validation in this plan is intentionally shallow: validate the current descriptor and immediate shape fields, but do not recursively walk the descriptor graph.
- No `DataStruct(...)` declaration DSL is introduced here. Explicit immutable descriptors are the v1 test surface.
- Public headers must compile as C11 and C++17.
- Use repository CMake presets; do not encode generator/toolchain logic in ad-hoc commands or CI YAML.
- TurboParser natural TurboSTL migration is already satisfied by `qigao/turbo-parser` PR #1 / merge commit `c9d0903e1134211431e7bce7dd0e5e1001746d9c`; there is no remaining Phase 0 implementation task.

---

## File Structure

`cmeta/include/cmeta/data.h` is the only new public semantic metadata header. It owns semantic kinds, versioned top-level descriptors, kind-specific immutable shape structures, initializer macros, and declarations for validation/lookup helpers. It must use forward declarations for `struct cmeta_type_desc`, `struct cmeta_struct_desc`, and `struct cmeta_enum_desc`, so including `cmeta/data.h` alone does not create an include cycle.

`cmeta/src/data.c` owns runtime validation and lookup helpers. It may include `cmeta/cmeta.h`, `cmeta/struct.h`, and `cmeta/enum.h`, because implementation code can depend on complete reflection definitions.

`cmeta/tests/cmeta_data_test.c` is the C11 semantic descriptor contract test. It constructs explicit scalar, struct, and variant descriptors and checks versioning, shallow shape validation, and lookup behavior. Optional/sequence/map storage construction is intentionally not frozen in this PR; their semantic shape types are declared now and their construction/lifecycle behavior is exercised in later CBind/container plans.

`cmeta/tests/cmeta_header_cpp_test.cpp` extends the existing C++ public-header regression so `cmeta_data_desc` and its initializer macro are proven consumable from C++17.

`cmeta/include/cmeta/cmeta.h`, `cmeta/CMakeLists.txt`, and `cmeta/tests/CMakeLists.txt` only integrate the new public header/source/test into the existing CMeta target and test surface.

---

### Task 1: Define the versioned semantic descriptor surface

**Files:**
- Create: `cmeta/include/cmeta/data.h`
- Create: `cmeta/tests/cmeta_data_test.c`
- Modify: `cmeta/include/cmeta/cmeta.h`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`
- Modify: `cmeta/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `cmeta_type_desc`, `cmeta_struct_desc`, `cmeta_enum_desc` identities by pointer only; existing `Struct(...)` and `Enum(...)` metadata remain unchanged.
- Produces: `cmeta_data_kind`, `cmeta_data_buffer_mode`, `cmeta_data_desc`, kind-specific shape structs, `CMETA_DATA_DESC_ABI_VERSION`, `CMETA_DATA_DESC_INIT`, and declarations for the validation/lookup API implemented in Task 2.

- [ ] **Step 1: Register a failing C11 semantic-data public-surface test**

Create `cmeta/tests/cmeta_data_test.c` with a minimal descriptor graph that uses the proposed API before the API exists:

```c
#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <cmeta/struct.h>
#include "tinytest.h"

#include <limits.h>
#include <stddef.h>

Struct(cmeta_data_person,
    (int, id)
);

static const cmeta_data_integer_shape cmeta_data_int_shape = {
    (uint8_t)(sizeof(int) * CHAR_BIT)
};

static const cmeta_data_desc cmeta_data_int_desc = CMETA_DATA_DESC_INIT(
    "c.int", "int", CMETA_DATA_SINT,
    &cmeta_type_int, &cmeta_data_int_shape, NULL);

static const cmeta_data_field_desc cmeta_data_person_fields[] = {
    {
        "cmeta_data_person.id",
        "id",
        offsetof(cmeta_data_person, id),
        &cmeta_data_int_desc
    }
};

static const cmeta_type_desc cmeta_data_person_storage = {
    .name = "cmeta_data_person",
    .size = sizeof(cmeta_data_person),
    .align = _Alignof(cmeta_data_person),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_data_struct_shape cmeta_data_person_shape = {
    StructMeta(cmeta_data_person),
    cmeta_data_person_fields,
    1u
};

static const cmeta_data_desc cmeta_data_person_desc = CMETA_DATA_DESC_INIT(
    "test.person", "cmeta_data_person", CMETA_DATA_STRUCT,
    &cmeta_data_person_storage, &cmeta_data_person_shape, NULL);

spec("CMeta semantic data descriptor surface") {
  it("keeps ABI versioning and semantic shape separate from layout reflection") {
    check_equal(cmeta_data_person_desc.struct_size, sizeof(cmeta_data_desc));
    check_equal(cmeta_data_person_desc.abi_version, CMETA_DATA_DESC_ABI_VERSION);
    check_equal(cmeta_data_person_desc.kind, CMETA_DATA_STRUCT);
    check(cmeta_data_person_desc.shape == &cmeta_data_person_shape);
    check(cmeta_data_person_shape.layout == StructMeta(cmeta_data_person));
    check_equal(cmeta_data_person_shape.fields[0].offset,
                offsetof(cmeta_data_person, id));
    check(cmeta_data_person_shape.fields[0].value == &cmeta_data_int_desc);
  }
}
```

Register the target in `cmeta/tests/CMakeLists.txt`:

```cmake
cmake_add_test(cmeta_data_test
  SOURCES cmeta_data_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

set_target_properties(cmeta_data_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

Also add `cmeta_data_test` to the existing GNU/Clang `-Werror=missing-field-initializers` loop so descriptor initializers stay complete.

- [ ] **Step 2: Run the focused build and verify it fails because the semantic-data API does not exist yet**

Run:

```bash
cmake --preset dev-linux-ninja
cmake --build --preset build-debug-linux --target cmeta_data_test
```

Expected: compilation fails on missing `<cmeta/data.h>` or undefined `cmeta_data_*` declarations. A failure caused by preset/toolchain configuration is not the expected red state and must be fixed before continuing.

- [ ] **Step 3: Add `cmeta/include/cmeta/data.h` with the exact v1 descriptor types**

Create the public header with this core shape:

```c
#ifndef CMETA_DATA_H
#define CMETA_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cmeta_type_desc;
struct cmeta_struct_desc;
struct cmeta_enum_desc;
struct cmeta_data_ops;

typedef enum cmeta_data_kind {
    CMETA_DATA_BOOL = 0,
    CMETA_DATA_SINT,
    CMETA_DATA_UINT,
    CMETA_DATA_FLOAT,
    CMETA_DATA_STRING,
    CMETA_DATA_BYTES,
    CMETA_DATA_ENUM,
    CMETA_DATA_STRUCT,
    CMETA_DATA_VARIANT,
    CMETA_DATA_OPTIONAL,
    CMETA_DATA_SEQUENCE,
    CMETA_DATA_MAP,
    CMETA_DATA_CUSTOM
} cmeta_data_kind;

typedef enum cmeta_data_buffer_mode {
    CMETA_DATA_BUFFER_OWNED = 0,
    CMETA_DATA_BUFFER_BORROWED,
    CMETA_DATA_BUFFER_CUSTOM
} cmeta_data_buffer_mode;

typedef struct cmeta_data_desc cmeta_data_desc;

typedef struct cmeta_data_integer_shape {
    uint8_t bits;
} cmeta_data_integer_shape;

typedef struct cmeta_data_float_shape {
    uint8_t bits;
} cmeta_data_float_shape;

typedef struct cmeta_data_buffer_shape {
    cmeta_data_buffer_mode mode;
} cmeta_data_buffer_shape;

typedef struct cmeta_data_enum_shape {
    const struct cmeta_enum_desc *reflection;
} cmeta_data_enum_shape;

typedef struct cmeta_data_field_desc {
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_field_desc;

typedef struct cmeta_data_struct_shape {
    const struct cmeta_struct_desc *layout;
    const cmeta_data_field_desc *fields;
    size_t field_count;
} cmeta_data_struct_shape;

typedef struct cmeta_data_variant_case_desc {
    int64_t tag_value;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_variant_case_desc;

typedef struct cmeta_data_variant_shape {
    size_t tag_offset;
    const cmeta_data_desc *tag;
    const cmeta_data_variant_case_desc *cases;
    size_t case_count;
} cmeta_data_variant_shape;

typedef struct cmeta_data_optional_shape {
    const cmeta_data_desc *value;
} cmeta_data_optional_shape;

typedef struct cmeta_data_sequence_shape {
    const cmeta_data_desc *element;
} cmeta_data_sequence_shape;

typedef struct cmeta_data_map_shape {
    const cmeta_data_desc *key;
    const cmeta_data_desc *value;
} cmeta_data_map_shape;

struct cmeta_data_desc {
    size_t struct_size;
    uint32_t abi_version;
    const char *stable_id;
    const char *display_name;
    cmeta_data_kind kind;
    const struct cmeta_type_desc *storage_type;
    const void *shape;
    const struct cmeta_data_ops *ops;
};

enum { CMETA_DATA_DESC_ABI_VERSION = 1u };

#define CMETA_DATA_DESC_INIT(id_, display_, kind_, storage_, shape_, ops_) \
    { sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,               \
      (id_), (display_), (kind_), (storage_), (shape_), (ops_) }

bool cmeta_data_kind_valid(cmeta_data_kind kind);
bool cmeta_data_desc_valid(const cmeta_data_desc *desc);

const cmeta_data_field_desc *
cmeta_data_struct_field(const cmeta_data_struct_shape *shape, size_t index);
const cmeta_data_field_desc *
cmeta_data_struct_find_field(const cmeta_data_struct_shape *shape,
                             const char *name);
const cmeta_data_variant_case_desc *
cmeta_data_variant_case_by_tag(const cmeta_data_variant_shape *shape,
                               int64_t tag_value);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_DATA_H */
```

Do not define `struct cmeta_data_ops` yet. Its pointer is an intentional extension point for later lifecycle/access plans; freezing callback signatures before CBind exists would be premature.

- [ ] **Step 4: Re-export the descriptor surface from `cmeta/cmeta.h` without creating an include cycle**

In `cmeta/include/cmeta/cmeta.h`, include the new header immediately after the complete `cmeta_type_desc` definition:

```c
typedef struct cmeta_type_desc {
    const char *name;
    size_t size;
    size_t align;
    cmeta_type_kind kind;
    const struct cmeta_type_desc *pointee;
    const cmeta_type_traits *traits;
    const cmeta_type_identity *identity;
} cmeta_type_desc;

#include <cmeta/data.h>
```

Do not make `data.h` include `cmeta.h`; `data.h` stays independently includable by using struct-tag forward declarations.

- [ ] **Step 5: Extend the existing C++ public-header regression before running green**

In `cmeta/tests/cmeta_header_cpp_test.cpp`, add `#include <climits>` and a C++17 aggregate initialization check using the public macro:

```cpp
static const cmeta_data_integer_shape cmeta_cpp_data_int_shape = {
    static_cast<uint8_t>(sizeof(int) * CHAR_BIT)
};

static const cmeta_data_desc cmeta_cpp_data_int = CMETA_DATA_DESC_INIT(
    "c.int", "int", CMETA_DATA_SINT,
    &cmeta_type_int, &cmeta_cpp_data_int_shape, nullptr);
```

Add one assertion inside the existing spec:

```cpp
it("exposes versioned semantic data descriptors to C++") {
  check_equal(cmeta_cpp_data_int.struct_size, sizeof(cmeta_data_desc));
  check_equal(cmeta_cpp_data_int.abi_version, CMETA_DATA_DESC_ABI_VERSION);
  check_equal(cmeta_cpp_data_int.kind, CMETA_DATA_SINT);
}
```

- [ ] **Step 6: Run the focused C and C++ public-surface tests**

Run:

```bash
cmake --build --preset build-debug-linux --target cmeta_data_test cmeta_header_cpp_test
ctest --preset test-dev-linux -R '^(cmeta_data_test|cmeta_header_cpp_test)$'
```

Expected: both tests pass. `cmeta_data_test` is not allowed to call `cmeta_data_desc_valid()` yet; Task 2 owns the runtime implementation.

- [ ] **Step 7: Commit the descriptor surface**

```bash
git add \
  cmeta/include/cmeta/data.h \
  cmeta/include/cmeta/cmeta.h \
  cmeta/tests/cmeta_data_test.c \
  cmeta/tests/cmeta_header_cpp_test.cpp \
  cmeta/tests/CMakeLists.txt
git commit -m "feat(cmeta): define semantic data descriptors"
```

---

### Task 2: Implement shallow validation and record/variant lookup helpers

**Files:**
- Create: `cmeta/src/data.c`
- Modify: `cmeta/CMakeLists.txt`
- Modify: `cmeta/tests/cmeta_data_test.c`

**Interfaces:**
- Consumes: the exact public types and declarations from Task 1.
- Produces: `cmeta_data_kind_valid()`, `cmeta_data_desc_valid()`, `cmeta_data_struct_field()`, `cmeta_data_struct_find_field()`, and `cmeta_data_variant_case_by_tag()` as compiled `TurboUtils::CMeta` symbols.

- [ ] **Step 1: Add failing runtime validation and lookup tests**

Extend `cmeta/tests/cmeta_data_test.c` with immediate-shape coverage. Use a real enum reflection for the variant tag:

```c
Enum(cmeta_data_choice_tag,
    (CMETA_DATA_CHOICE_INT, 1, "int"),
    (CMETA_DATA_CHOICE_TEXT, 2, "text")
);

static const cmeta_type_desc cmeta_data_choice_tag_storage = {
    .name = "cmeta_data_choice_tag",
    .size = sizeof(cmeta_data_choice_tag),
    .align = _Alignof(cmeta_data_choice_tag),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_data_enum_shape cmeta_data_choice_tag_shape = {
    EnumMeta(cmeta_data_choice_tag)
};

static const cmeta_data_desc cmeta_data_choice_tag_desc = CMETA_DATA_DESC_INIT(
    "test.choice.tag", "cmeta_data_choice_tag", CMETA_DATA_ENUM,
    &cmeta_data_choice_tag_storage, &cmeta_data_choice_tag_shape, NULL);

typedef union cmeta_data_choice_storage {
    int integer;
    int text_placeholder;
} cmeta_data_choice_storage;

typedef struct cmeta_data_choice {
    cmeta_data_choice_tag tag;
    cmeta_data_choice_storage value;
} cmeta_data_choice;

static const cmeta_data_variant_case_desc cmeta_data_choice_cases[] = {
    {
        CMETA_DATA_CHOICE_INT,
        "int",
        offsetof(cmeta_data_choice, value) +
            offsetof(cmeta_data_choice_storage, integer),
        &cmeta_data_int_desc
    },
    {
        CMETA_DATA_CHOICE_TEXT,
        "text",
        offsetof(cmeta_data_choice, value) +
            offsetof(cmeta_data_choice_storage, text_placeholder),
        &cmeta_data_int_desc
    }
};

static const cmeta_data_variant_shape cmeta_data_choice_shape = {
    offsetof(cmeta_data_choice, tag),
    &cmeta_data_choice_tag_desc,
    cmeta_data_choice_cases,
    2u
};

static const cmeta_type_desc cmeta_data_choice_storage_type = {
    .name = "cmeta_data_choice",
    .size = sizeof(cmeta_data_choice),
    .align = _Alignof(cmeta_data_choice),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_data_desc cmeta_data_choice_desc = CMETA_DATA_DESC_INIT(
    "test.choice", "cmeta_data_choice", CMETA_DATA_VARIANT,
    &cmeta_data_choice_storage_type, &cmeta_data_choice_shape, NULL);
```

Add these tests:

```c
it("validates supported semantic descriptor headers and immediate shapes") {
  check_true(cmeta_data_kind_valid(CMETA_DATA_BOOL));
  check_true(cmeta_data_kind_valid(CMETA_DATA_VARIANT));
  check_false(cmeta_data_kind_valid((cmeta_data_kind)-1));
  check_false(cmeta_data_kind_valid((cmeta_data_kind)(CMETA_DATA_CUSTOM + 1)));

  check_true(cmeta_data_desc_valid(&cmeta_data_int_desc));
  check_true(cmeta_data_desc_valid(&cmeta_data_person_desc));
  check_true(cmeta_data_desc_valid(&cmeta_data_choice_desc));
}

it("rejects ABI and kind-specific shape errors deterministically") {
  cmeta_data_desc bad_abi = cmeta_data_int_desc;
  cmeta_data_desc missing_shape = cmeta_data_int_desc;

  bad_abi.abi_version = CMETA_DATA_DESC_ABI_VERSION + 1u;
  missing_shape.shape = NULL;

  check_false(cmeta_data_desc_valid(&bad_abi));
  check_false(cmeta_data_desc_valid(&missing_shape));
}

it("looks up struct fields and variant alternatives without pointer identity tricks") {
  const cmeta_data_field_desc *id =
      cmeta_data_struct_find_field(&cmeta_data_person_shape, "id");
  const cmeta_data_variant_case_desc *choice =
      cmeta_data_variant_case_by_tag(&cmeta_data_choice_shape,
                                     CMETA_DATA_CHOICE_TEXT);

  check(id == &cmeta_data_person_fields[0]);
  check(cmeta_data_struct_field(&cmeta_data_person_shape, 0u) == id);
  check_null(cmeta_data_struct_field(&cmeta_data_person_shape, 1u));
  check(choice == &cmeta_data_choice_cases[1]);
  check_null(cmeta_data_variant_case_by_tag(&cmeta_data_choice_shape, 99));
}
```

- [ ] **Step 2: Run the focused test and verify the link fails because the declared runtime functions have no implementation**

Run:

```bash
cmake --build --preset build-debug-linux --target cmeta_data_test
```

Expected: link failure for one or more of `cmeta_data_kind_valid`, `cmeta_data_desc_valid`, `cmeta_data_struct_field`, `cmeta_data_struct_find_field`, or `cmeta_data_variant_case_by_tag`.

- [ ] **Step 3: Implement `cmeta/src/data.c` with shallow validation**

Create `cmeta/src/data.c` using this structure:

```c
#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <cmeta/enum.h>
#include <cmeta/struct.h>

#include <string.h>

static bool cmeta_data_text_valid(const char *text) {
    return text != NULL && text[0] != '\0';
}

static bool cmeta_data_bit_width_valid(uint8_t bits) {
    return bits == 8u || bits == 16u || bits == 32u || bits == 64u;
}

static bool cmeta_data_buffer_mode_valid(cmeta_data_buffer_mode mode) {
    return mode >= CMETA_DATA_BUFFER_OWNED &&
           mode <= CMETA_DATA_BUFFER_CUSTOM;
}

bool cmeta_data_kind_valid(cmeta_data_kind kind) {
    return kind >= CMETA_DATA_BOOL && kind <= CMETA_DATA_CUSTOM;
}

const cmeta_data_field_desc *
cmeta_data_struct_field(const cmeta_data_struct_shape *shape, size_t index) {
    return shape != NULL && index < shape->field_count
               ? &shape->fields[index]
               : NULL;
}

const cmeta_data_field_desc *
cmeta_data_struct_find_field(const cmeta_data_struct_shape *shape,
                             const char *name) {
    size_t i;
    if (shape == NULL || name == NULL) return NULL;
    for (i = 0; i < shape->field_count; ++i)
        if (shape->fields[i].name != NULL &&
            strcmp(shape->fields[i].name, name) == 0)
            return &shape->fields[i];
    return NULL;
}

const cmeta_data_variant_case_desc *
cmeta_data_variant_case_by_tag(const cmeta_data_variant_shape *shape,
                               int64_t tag_value) {
    size_t i;
    if (shape == NULL) return NULL;
    for (i = 0; i < shape->case_count; ++i)
        if (shape->cases[i].tag_value == tag_value)
            return &shape->cases[i];
    return NULL;
}
```

Implement `cmeta_data_desc_valid()` as a **non-recursive** switch. The exact required checks are:

```c
bool cmeta_data_desc_valid(const cmeta_data_desc *desc) {
    if (desc == NULL ||
        desc->struct_size < sizeof(cmeta_data_desc) ||
        desc->abi_version != CMETA_DATA_DESC_ABI_VERSION ||
        !cmeta_data_text_valid(desc->stable_id) ||
        !cmeta_data_text_valid(desc->display_name) ||
        !cmeta_data_kind_valid(desc->kind) ||
        desc->storage_type == NULL ||
        !cmeta_type_desc_valid(desc->storage_type))
        return false;

    switch (desc->kind) {
    case CMETA_DATA_BOOL:
        return true;
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT: {
        const cmeta_data_integer_shape *shape =
            (const cmeta_data_integer_shape *)desc->shape;
        return shape != NULL && cmeta_data_bit_width_valid(shape->bits);
    }
    case CMETA_DATA_FLOAT: {
        const cmeta_data_float_shape *shape =
            (const cmeta_data_float_shape *)desc->shape;
        return shape != NULL &&
               (shape->bits == 32u || shape->bits == 64u);
    }
    case CMETA_DATA_STRING:
    case CMETA_DATA_BYTES: {
        const cmeta_data_buffer_shape *shape =
            (const cmeta_data_buffer_shape *)desc->shape;
        return shape != NULL && cmeta_data_buffer_mode_valid(shape->mode);
    }
    case CMETA_DATA_ENUM: {
        const cmeta_data_enum_shape *shape =
            (const cmeta_data_enum_shape *)desc->shape;
        return shape != NULL && shape->reflection != NULL;
    }
    case CMETA_DATA_STRUCT: {
        const cmeta_data_struct_shape *shape =
            (const cmeta_data_struct_shape *)desc->shape;
        size_t i;
        if (shape == NULL || shape->layout == NULL ||
            (shape->field_count != 0u && shape->fields == NULL))
            return false;
        for (i = 0; i < shape->field_count; ++i) {
            const cmeta_data_field_desc *field = &shape->fields[i];
            if (!cmeta_data_text_valid(field->stable_id) ||
                !cmeta_data_text_valid(field->name) ||
                field->value == NULL || field->offset >= desc->storage_type->size)
                return false;
        }
        return true;
    }
    case CMETA_DATA_VARIANT: {
        const cmeta_data_variant_shape *shape =
            (const cmeta_data_variant_shape *)desc->shape;
        size_t i;
        if (shape == NULL || shape->tag == NULL ||
            (shape->tag->kind != CMETA_DATA_ENUM &&
             shape->tag->kind != CMETA_DATA_SINT &&
             shape->tag->kind != CMETA_DATA_UINT) ||
            shape->tag_offset >= desc->storage_type->size ||
            shape->case_count == 0u || shape->cases == NULL)
            return false;
        for (i = 0; i < shape->case_count; ++i) {
            if (!cmeta_data_text_valid(shape->cases[i].name) ||
                shape->cases[i].value == NULL ||
                shape->cases[i].offset >= desc->storage_type->size)
                return false;
        }
        return true;
    }
    case CMETA_DATA_OPTIONAL: {
        const cmeta_data_optional_shape *shape =
            (const cmeta_data_optional_shape *)desc->shape;
        return shape != NULL && shape->value != NULL;
    }
    case CMETA_DATA_SEQUENCE: {
        const cmeta_data_sequence_shape *shape =
            (const cmeta_data_sequence_shape *)desc->shape;
        return shape != NULL && shape->element != NULL;
    }
    case CMETA_DATA_MAP: {
        const cmeta_data_map_shape *shape =
            (const cmeta_data_map_shape *)desc->shape;
        return shape != NULL && shape->key != NULL && shape->value != NULL;
    }
    case CMETA_DATA_CUSTOM:
        return desc->shape != NULL || desc->ops != NULL;
    default:
        return false;
    }
}
```

Do not call `cmeta_data_desc_valid()` recursively on child pointers. That would create cycle/graph semantics before fingerprint/graph validation has been designed.

- [ ] **Step 4: Add `data.c` to the CMeta library target**

Modify `cmeta/CMakeLists.txt`:

```cmake
add_library(${TARGET_NAME}
  src/cmeta.c
  src/data.c
  src/entry.c
  src/type_identity.c)
```

No new library target is created; semantic metadata is part of `TurboUtils::CMeta`.

- [ ] **Step 5: Run the focused semantic-data tests**

Run:

```bash
cmake --build --preset build-debug-linux --target cmeta_data_test cmeta_header_cpp_test
ctest --preset test-dev-linux -R '^(cmeta_data_test|cmeta_header_cpp_test)$'
```

Expected: both tests pass, including wrong-ABI rejection and record/variant lookup behavior.

- [ ] **Step 6: Run the complete CMeta regression surface**

Run:

```bash
cmake --build --preset build-debug-linux --target \
  cmeta_core_test \
  cmeta_header_cpp_test \
  cmeta_meta_header_test \
  cmeta_language_surface_test \
  cmeta_collector_test \
  cmeta_data_test
ctest --preset test-dev-linux -R '^cmeta_'
```

Expected: all CMeta tests pass. Pay special attention to `cmeta_header_cpp_test` and GNU/Clang missing-field-initializer warnings; the new public types must not force changes to existing `cmeta_type_desc`, `cmeta_struct_desc`, or `cmeta_enum_desc` initializers.

- [ ] **Step 7: Run formatting/diff hygiene and commit the runtime implementation**

Run:

```bash
git diff --check
```

Expected: no whitespace errors.

Then commit:

```bash
git add \
  cmeta/src/data.c \
  cmeta/CMakeLists.txt \
  cmeta/tests/cmeta_data_test.c
git commit -m "feat(cmeta): validate semantic data descriptors"
```

---

## PR Verification Gate

Before the implementation PR is marked ready or merged, run a fresh Linux configure/build/test using the repository presets:

```bash
cmake --preset dev-linux-ninja
cmake --build --preset build-debug-linux
ctest --preset test-dev-linux
```

The PR must also run the repository's fresh Windows CI path. Do not claim Windows compatibility from the C++ header test alone.

Audit the implementation diff before review:

```bash
git diff --check
rg -n "json|yaml|xml|csv|DataBind|TbeTyped|cserde|cbind" cmeta/include/cmeta/data.h cmeta/src/data.c
```

Expected for the audit: no concrete format/parser/DataBind/CSerde/CBind dependency is introduced. Comments may mention architectural terms only if necessary to state a boundary; no format-specific types or callbacks may appear in the public descriptor API.

## Explicitly Deferred to Later Plans

Semantic/schema fingerprint hashing and graph traversal are deferred to the schema-identity plan. `cmeta_data_ops` callback signatures and owned/borrowed construction behavior are deferred until CBind lifecycle requirements are concrete. `cmeta_container_desc.construct` / `bind_types` is a separate plan. CSerde token protocol, CBind encode/decode, TurboParser adapters, DataBind migration, TBE wire descriptor separation, and `DataStruct(...)` DSL sugar are all outside this PR.
