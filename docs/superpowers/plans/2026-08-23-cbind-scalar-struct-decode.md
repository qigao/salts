# CBind Scalar + Struct Decode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `TurboUtils::CBind`, a C11 format-neutral decode kernel that converts CSerde canonical tokens into proven CMeta bool/integer/float/struct storage with strict preflight, exact numeric conversion, bounded caller scratch, and transactional destination rollback.

**Architecture:** `cbind_decode()` is the only D2 public operation. It validates caller ABI records, recursively proves the complete semantic/storage graph and resource budget before reading input, checks that the destination semantic graph is empty, then performs forward-only recursive decode through `cserde_reader`. Scalar conversion is isolated in `scalar.c`; struct graph/scratch/state-machine work is isolated in `struct.c`; `decode.c` owns public orchestration, source-status mapping, rollback, and error attribution. D2 deliberately does not expose an incremental token-fed machine, parser adapter, CFlow adapter, allocator, or container/string lifecycle.

**Tech Stack:** ISO C11, C++17 public-header/linkage compatibility, CMeta semantic descriptors/reflection, CSerde canonical reader, TinyTest, CMake presets, CTest, GitHub Actions Linux/MSVC release gates.

**Spec:**
- `docs/superpowers/specs/2026-08-23-cbind-scalar-struct-decode-design.md`
- `docs/superpowers/specs/2026-08-23-cbind-parser-integration-amendment.md`

## Global Constraints

- Repository ownership remains `qigao/turbo-utils`; public target is exactly `TurboUtils::CBind`, concrete target `turbo_cbind`, export name `CBind`.
- Execution must start from the latest `master`, not historical design base `4216bb8c71cf6564bf88fe6cff3c8a1c227c87d3`. At plan-writing time current master is `5972c4ee986d54befebbc3b4dcb535082a9286cd`; if master advances, use the newer exact head.
- Before Task 1 execution, create an isolated worktree/feature branch `feat/cbind-scalar-struct-decode` from that latest master, then cherry-pick the approved docs commits `9d47be11fb2a27a50e6928212c6b172a7761eeb8`, `fa456eb00cd3fcf9672e816b6961ff99f95035f5`, and the commit containing this plan so the spec travels with implementation. Do not rebase production work onto the historical design branch.
- Production dependency is exactly `TurboUtils::CMeta + TurboUtils::CSerde`. No production CBind file may include/link TurboSTL, Core/utils, CFlow, TurboParser, or concrete parser code.
- D2 public API is context-first and decode-only: `cbind_decode(ctx, shape, reader, out, error)`.
- Supported semantic kinds are exactly `CMETA_DATA_BOOL`, `CMETA_DATA_SINT`, `CMETA_DATA_UINT`, `CMETA_DATA_FLOAT`, and `CMETA_DATA_STRUCT`.
- Valid STRING/BYTES/ENUM/VARIANT/SEQUENCE/SET/MAP/CUSTOM descriptors return `CBIND_UNSUPPORTED` before reader consumption.
- Canonical writable scalar storage is limited to CMeta built-ins: bool; int/long; size_t; float/double. Match canonical storage identity with `cmeta_type_equal`, then separately prove exact size/alignment/semantic width; do not use raw descriptor-pointer equality as the sole identity test.
- All validation/resource/destination-empty failures happen before the first `cserde_reader_next()` call and must leave reader/provider state and destination unchanged.
- Struct depth semantics are scalar root `0`, root struct `1`, nested struct `2+`.
- Scratch is caller-owned bookkeeping only. Per active struct bitmap bytes are computed overflow-safely as `field_count / 8u + (field_count % 8u != 0u)`; never evaluate `(field_count + 7u) / 8u` without guarding overflow.
- No fixed field-count limit and no production heap allocation. D2 production files must not call `malloc`, `calloc`, `realloc`, or `free`.
- Decode failure after consumption restores every supported semantic field of the root destination to the D2 empty state; padding and reflected-but-nonsemantic fields remain untouched. Reader position is never rewound and no remainder is silently skipped.
- CSerde `DONE` while a token is required maps to `CBIND_UNEXPECTED_END` with `source_status=CSERDE_DONE`; every other non-OK reader result maps to `CBIND_SOURCE_ERROR` with the exact returned `cserde_status`.
- CSerde recording support remains BUILD_TESTS-only and is reused by CBind tests through `cserde_recording_support`; do not copy/fork a second recording token source into CBind.
- Raw `cserde_token` is structural transport, not a CFlow business stream. D2 adds no CFlow or parser integration and no public `begin/feed/finish` decoder ABI.
- Public headers compile/link in C11 and C++17; no public `_Generic` or GNU-only declarations.
- Final exact implementation head must pass fresh Linux and Windows release configure/build/test with `cbind_*` included in `.github/workflows/cmeta.yml`.

---

## File Structure

Production files after D2:

```text
cbind/
  CMakeLists.txt
  include/cbind/
    cbind.h
    status.h
    context.h
    error.h
    decode.h
  src/
    decode.c
    scalar.c
    struct.c
    internal.h
```

Test files after D2:

```text
cbind/tests/
  CMakeLists.txt
  cbind_scalar_decode_test.c
  cbind_struct_decode_test.c
  cbind_header_cpp_test.cpp
```

Repository files modified:

```text
CMakeLists.txt
.github/workflows/cmeta.yml
```

`cbind/tests` reuses the existing global BUILD_TESTS-only target `cserde_recording_support` created by `cserde/tests/CMakeLists.txt` because root module order keeps `cserde` before `cbind`.

---

### Task 1: Module boundary, public ABI records, package export, and CI discovery

**Files:**
- Create: `cbind/CMakeLists.txt`
- Create: `cbind/include/cbind/status.h`
- Create: `cbind/include/cbind/context.h`
- Create: `cbind/include/cbind/error.h`
- Create: `cbind/include/cbind/decode.h`
- Create: `cbind/include/cbind/cbind.h`
- Create: `cbind/src/decode.c`
- Create: `cbind/src/internal.h`
- Create: `cbind/tests/CMakeLists.txt`
- Create: `cbind/tests/cbind_scalar_decode_test.c`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: `TurboUtils::CMeta`, `TurboUtils::CSerde`, test-only `TurboUtils::TinyTest` and `cserde_recording_support`.
- Produces public declarations:

```c
typedef enum cbind_status {
    CBIND_OK = 0,
    CBIND_INVALID_ARGUMENT,
    CBIND_INVALID_CONTEXT,
    CBIND_INVALID_SHAPE,
    CBIND_DESTINATION_NOT_EMPTY,
    CBIND_TOKEN_MISMATCH,
    CBIND_VALUE_OUT_OF_RANGE,
    CBIND_UNKNOWN_FIELD,
    CBIND_DUPLICATE_FIELD,
    CBIND_MISSING_FIELD,
    CBIND_UNEXPECTED_END,
    CBIND_LIMIT_EXCEEDED,
    CBIND_UNSUPPORTED,
    CBIND_SOURCE_ERROR
} cbind_status;

enum { CBIND_CONTEXT_ABI_VERSION = 1u };
typedef struct cbind_context {
    size_t struct_size;
    uint32_t abi_version;
    void *scratch;
    size_t scratch_size;
    size_t max_depth;
} cbind_context;

#define CBIND_CONTEXT_INIT(scratch_ptr, scratch_bytes, depth_limit) \
    { sizeof(cbind_context), CBIND_CONTEXT_ABI_VERSION, \
      (scratch_ptr), (scratch_bytes), (depth_limit) }

enum { CBIND_ERROR_ABI_VERSION = 1u };
typedef struct cbind_error {
    size_t struct_size;
    uint32_t abi_version;
    cbind_status status;
    cserde_status source_status;
    const cmeta_data_desc *shape;
    const cmeta_data_field_desc *field;
    size_t depth;
} cbind_error;

#define CBIND_ERROR_INIT \
    { sizeof(cbind_error), CBIND_ERROR_ABI_VERSION, CBIND_OK, \
      CSERDE_OK, NULL, NULL, 0u }

cbind_status cbind_decode(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    void *out,
    cbind_error *error);
```

- Produces internal prefix helpers in `internal.h` used by later tasks:

```c
#define CBIND_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))

bool cbind_context_valid(const cbind_context *context);
bool cbind_error_valid(const cbind_error *error);
void cbind_error_clear(cbind_error *error);
void cbind_error_set(cbind_error *error,
                     cbind_status status,
                     cserde_status source_status,
                     const cmeta_data_desc *shape,
                     const cmeta_data_field_desc *field,
                     size_t depth);
```

- [ ] **Step 1: Add module/test scaffold and ABI declarations, but leave initializer macros absent for the first RED**

`cbind/CMakeLists.txt` follows current CMeta/CSerde target style:

```cmake
set(TARGET_NAME turbo_cbind)

add_library(${TARGET_NAME}
  src/decode.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::CBind
  FOLDER "cbind"
  EXPORT_NAME CBind)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME}
  PUBLIC TurboUtils::CMeta TurboUtils::CSerde)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cbind
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

Root `CMakeLists.txt` becomes:

```cmake
add_subdirectory(cmeta)
add_subdirectory(cserde)
add_subdirectory(cbind)
add_subdirectory(cflow)
```

Initial `cbind/tests/CMakeLists.txt`:

```cmake
cmake_add_test(cbind_scalar_decode_test
  SOURCES cbind_scalar_decode_test.c
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

set_target_properties(cbind_scalar_decode_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

At this RED head, declare the structs/enum/function but intentionally omit `CBIND_CONTEXT_INIT` and `CBIND_ERROR_INIT`. `src/decode.c` may contain the internal ABI helpers but does not need to define `cbind_decode` until Task 2 because Task 1 tests only the record declarations/macros.

Modify `.github/workflows/cmeta.yml` in both `pull_request.paths` and `push.paths`:

```yaml
      - "cbind/**"
```

Change both selected-test regexes to:

```text
^(cmeta_|cserde_|cbind_|cflow_|turbostl_)
```

Rename the Linux step to `Test CMeta, CSerde, CBind, CFlow, and TurboSTL`.

- [ ] **Step 2: Write ABI RED tests**

Start `cbind/tests/cbind_scalar_decode_test.c` with:

```c
#include <cbind/cbind.h>
#include "tinytest.h"

#include <stddef.h>

spec("CBind public ABI records") {
  it("initializes a caller-sized context") {
    unsigned char scratch[3] = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, sizeof(scratch), 2u);

    check_equal(context.struct_size, sizeof(cbind_context));
    check_equal(context.abi_version, (uint32_t)CBIND_CONTEXT_ABI_VERSION);
    check_true(context.scratch == scratch);
    check_equal(context.scratch_size, sizeof(scratch));
    check_equal(context.max_depth, (size_t)2u);
  }

  it("initializes a caller-sized error record") {
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(error.struct_size, sizeof(cbind_error));
    check_equal(error.abi_version, (uint32_t)CBIND_ERROR_ABI_VERSION);
    check_equal(error.status, CBIND_OK);
    check_equal(error.source_status, CSERDE_OK);
    check_null(error.shape);
    check_null(error.field);
    check_equal(error.depth, (size_t)0u);
  }
}
```

Also add compile-time/runtime checks that the enum ordering starts at `CBIND_OK == 0` and versions are exactly `1u`.

- [ ] **Step 3: Run RED and commit test contract**

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
```

Expected: compile fails only because `CBIND_CONTEXT_INIT` / `CBIND_ERROR_INIT` are not yet defined.

```bash
git add CMakeLists.txt .github/workflows/cmeta.yml cbind

git commit -m "test(cbind): define public ABI contract"
```

Record this exact RED head SHA and failing diagnostic in the draft PR/TDD notes when the PR is opened during execution.

- [ ] **Step 4: Add header-side initializers and internal ABI validation helpers**

`context.h` and `error.h` add the exact macros above. `decode.h` wraps only `cbind_decode` in standard C linkage guards. `cbind.h` includes `status.h`, `context.h`, `error.h`, and `decode.h`.

`src/internal.h` declares the helper signatures above. `src/decode.c` implements:

```c
bool cbind_context_valid(const cbind_context *context) {
    return context != NULL &&
           context->struct_size >= CBIND_FIELD_END(cbind_context, max_depth) &&
           context->abi_version == CBIND_CONTEXT_ABI_VERSION &&
           (context->scratch_size == 0u || context->scratch != NULL);
}

bool cbind_error_valid(const cbind_error *error) {
    return error != NULL &&
           error->struct_size >= CBIND_FIELD_END(cbind_error, depth) &&
           error->abi_version == CBIND_ERROR_ABI_VERSION;
}
```

`cbind_error_clear()` writes only the v1 fields after validation has succeeded. `cbind_error_set()` writes `status/source_status/shape/field/depth` and does not dereference a NULL optional error pointer.

- [ ] **Step 5: Run GREEN and audit target boundary**

```bash
cmake --build --preset build-default-linux --target turbo_cbind cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
```

Expected: PASS.

Audit:

```bash
rg -n "target_link_libraries|TurboUtils::(STL|Core|CFlow)|TurboParser" cbind CMakeLists.txt
```

Expected production link line contains only `TurboUtils::CMeta TurboUtils::CSerde`; forbidden module names do not occur in production CBind sources/headers.

- [ ] **Step 6: Commit Task 1 GREEN**

```bash
git add cbind CMakeLists.txt .github/workflows/cmeta.yml
git commit -m "feat(cbind): add public ABI shell"
```

---

### Task 2: Scalar graph preflight, destination-empty proof, reader mapping, BOOL and integer decode

**Files:**
- Create: `cbind/src/scalar.c`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/CMakeLists.txt`
- Modify: `cbind/tests/cbind_scalar_decode_test.c`

**Interfaces:**
- Consumes: Task 1 public ABI and current CMeta/CSerde contracts.
- Produces internal scalar/preflight helpers:

```c
typedef struct cbind_decode_state {
    const cbind_context *context;
    cserde_reader *reader;
    cbind_error *error;
    unsigned char *scratch;
    size_t scratch_used;
} cbind_decode_state;

typedef struct cbind_validation_frame {
    const cmeta_data_desc *shape;
    const struct cbind_validation_frame *parent;
} cbind_validation_frame;

cbind_status cbind_validate_graph(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    const cbind_validation_frame *parent,
    size_t active_scratch,
    size_t *max_scratch,
    cbind_error *error);

bool cbind_value_is_empty(const cmeta_data_desc *shape, const void *value);
void cbind_value_reset(const cmeta_data_desc *shape, void *value);

cbind_status cbind_read_required(
    cbind_decode_state *state,
    cserde_token *token,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth);

cbind_status cbind_decode_scalar(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    void *out);
```

At the end of this task, scalar roots BOOL/SINT/UINT are complete; FLOAT receives its full numeric conversion contract in Task 3. Struct-specific recursive proof is completed in Task 4.

- [ ] **Step 1: Write scalar RED cases using the existing CSerde recording provider**

Add:

```c
#include "recording.h"

static void init_recording_reader(cserde_reader *reader,
                                  cserde_recording_reader_context *context,
                                  const cserde_token *tokens,
                                  size_t count) {
    *reader = (cserde_reader){0};
    *context = (cserde_recording_reader_context){ tokens, count, 0u };
    check_equal(cserde_reader_init(reader, &cserde_recording_reader_ops, context),
                CSERDE_OK);
}
```

Concrete tests must include:

```c
it("decodes BOOL without scratch at depth zero") {
    const cserde_token tokens[] = {
        { .kind = CSERDE_BOOL, .value.boolean = true }
    };
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    bool out = false;

    init_recording_reader(&reader, &source, tokens, 1u);
    check_equal(cbind_decode(&context, &cmeta_data_bool, &reader, &out, &error),
                CBIND_OK);
    check_true(out);
    check_equal(source.index, (size_t)1u);
    check_equal(error.status, CBIND_OK);
}
```

Add explicit cases for:

```text
BOOL <- SINT                    => TOKEN_MISMATCH, out reset false
int  <- SINT(INT_MAX)           => OK
int  <- SINT(INT_MIN)           => OK
long <- SINT(LONG_MAX/MIN)      => OK, platform-derived
int  <- UINT(INT_MAX)           => OK
int  <- UINT(INT_MAX+1)         => VALUE_OUT_OF_RANGE where representable
size <- UINT(SIZE_MAX)          => OK
size <- SINT(-1)                => VALUE_OUT_OF_RANGE
STRING token -> int             => TOKEN_MISMATCH
```

Use conditional construction for one-step overflow so the test never invokes undefined compile-time conversion on platforms where source and destination maxima coincide.

Add pre-consumption cases asserting `source.index == 0u`:

```text
context == NULL                                => INVALID_CONTEXT or INVALID_ARGUMENT per API ordering below
context.struct_size one byte short             => INVALID_CONTEXT
context wrong ABI                              => INVALID_CONTEXT
scratch_size > 0 with scratch == NULL          => INVALID_CONTEXT
malformed non-NULL error prefix                => INVALID_ARGUMENT
shape == NULL / reader == NULL / out == NULL   => INVALID_ARGUMENT
valid STRING semantic descriptor               => UNSUPPORTED
forged canonical int width mismatch            => INVALID_SHAPE
valid noncanonical scalar storage identity      => UNSUPPORTED
non-empty int destination                       => DESTINATION_NOT_EMPTY
```

Normative public validation order in tests is:

```text
required pointer arguments
optional error prefix
context
semantic/storage preflight
resource preflight
destination empty state
reader consumption
```

- [ ] **Step 2: Run scalar RED**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
```

Expected: link fails on `cbind_decode` (or tests fail because scalar decode is absent if Task 1 already introduced a temporary definition). No unrelated CMeta/CSerde failure is acceptable.

```bash
git add cbind/tests/cbind_scalar_decode_test.c cbind/src cbind/CMakeLists.txt
git commit -m "test(cbind): specify scalar decode preflight"
```

- [ ] **Step 3: Implement public orchestration and source-status mapping**

`decode.c` implements the public entry with no reader call before preflight/empty checks:

```c
cbind_status cbind_decode(const cbind_context *context,
                          const cmeta_data_desc *shape,
                          cserde_reader *reader,
                          void *out,
                          cbind_error *error) {
    cbind_decode_state state;
    size_t max_scratch = 0u;
    cbind_status status;

    if (shape == NULL || reader == NULL || out == NULL)
        return CBIND_INVALID_ARGUMENT;
    if (error != NULL && !cbind_error_valid(error))
        return CBIND_INVALID_ARGUMENT;
    if (!cbind_context_valid(context)) {
        cbind_error_set(error, CBIND_INVALID_CONTEXT, CSERDE_OK,
                        shape, NULL, 0u);
        return CBIND_INVALID_CONTEXT;
    }

    status = cbind_validate_graph(context, shape, 0u, NULL, 0u,
                                  &max_scratch, error);
    if (status != CBIND_OK)
        return status;
    if (max_scratch > context->scratch_size) {
        cbind_error_set(error, CBIND_LIMIT_EXCEEDED, CSERDE_OK,
                        shape, NULL, 0u);
        return CBIND_LIMIT_EXCEEDED;
    }
    if (!cbind_value_is_empty(shape, out)) {
        cbind_error_set(error, CBIND_DESTINATION_NOT_EMPTY, CSERDE_OK,
                        shape, NULL, 0u);
        return CBIND_DESTINATION_NOT_EMPTY;
    }

    state.context = context;
    state.reader = reader;
    state.error = error;
    state.scratch = (unsigned char *)context->scratch;
    state.scratch_used = 0u;

    cbind_error_clear(error);
    status = cbind_decode_scalar(&state, shape, NULL, 0u, out);
    if (status != CBIND_OK)
        cbind_value_reset(shape, out);
    return status;
}
```

This exact final root dispatch is generalized to `cbind_decode_value()` in Task 5 when structs become runtime-decodable; do not expose that helper publicly.

`cbind_read_required()` is the single reader mapping point:

```c
cserde_status source = cserde_reader_next(state->reader, token);
if (source == CSERDE_OK)
    return CBIND_OK;
if (source == CSERDE_DONE) {
    cbind_error_set(state->error, CBIND_UNEXPECTED_END, CSERDE_DONE,
                    shape, field, depth);
    return CBIND_UNEXPECTED_END;
}
cbind_error_set(state->error, CBIND_SOURCE_ERROR, source,
                shape, field, depth);
return CBIND_SOURCE_ERROR;
```

- [ ] **Step 4: Implement scalar storage proof and semantic empty/reset using `memcpy`**

In `scalar.c`, canonical matching must use `cmeta_type_equal()` plus size/alignment/width checks. Pattern:

```c
static bool cbind_type_matches(const cmeta_type_desc *actual,
                               const cmeta_type_desc *canonical) {
    return actual != NULL && canonical != NULL &&
           cmeta_type_equal(actual, canonical) &&
           actual->size == canonical->size &&
           actual->align == canonical->align;
}
```

BOOL accepts only canonical bool storage. SINT accepts canonical int or long. UINT accepts canonical size_t. FLOAT accepts canonical float or double. For SINT/UINT/FLOAT, prove shape bits equal `storage_type->size * CHAR_BIT`.

For native read/write/reset use typed locals + `memcpy`, e.g.:

```c
static bool cbind_int_empty(const void *src) {
    int value;
    memcpy(&value, src, sizeof(value));
    return value == 0;
}

static void cbind_int_store(void *dst, int value) {
    memcpy(dst, &value, sizeof(value));
}
```

Do not dereference erased destination as `(int *)out`, `(long *)out`, etc.

- [ ] **Step 5: Implement BOOL and integer conversions without pre-check casts**

For signed token to signed target, compare source `int64_t` against target macros before narrowing. For UINT to signed target, compare against `(uint64_t)INT_MAX` / `(uint64_t)LONG_MAX`. For signed to `size_t`, reject negative first, then compare as `uint64_t` when `SIZE_MAX < UINT64_MAX`.

Representative branch:

```c
if (cbind_type_matches(shape->storage_type, &cmeta_type_int)) {
    int value;
    if (token.kind == CSERDE_SINT) {
        if (token.value.sint < (int64_t)INT_MIN ||
            token.value.sint > (int64_t)INT_MAX)
            return cbind_scalar_range_error(...);
        value = (int)token.value.sint;
    } else if (token.kind == CSERDE_UINT) {
        if (token.value.uint > (uint64_t)INT_MAX)
            return cbind_scalar_range_error(...);
        value = (int)token.value.uint;
    } else {
        return cbind_scalar_token_error(...);
    }
    memcpy(out, &value, sizeof(value));
    return CBIND_OK;
}
```

FLOAT-to-integer support is added in Task 3; until then its RED tests remain absent.

- [ ] **Step 6: Run scalar GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
```

Expected: all Task 1/2 scalar cases PASS. Also run:

```bash
ctest --preset test-release-linux -R '^(cmeta_data_test|cserde_)' --output-on-failure
```

Expected: existing semantic/token substrate remains PASS.

- [ ] **Step 7: Commit Task 2 GREEN**

```bash
git add cbind
git commit -m "feat(cbind): decode canonical bool and integers"
```

---

### Task 3: Strict FLOAT-to-integer, FLOAT-to-float, and exact integer-to-float conversion

**Files:**
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/tests/cbind_scalar_decode_test.c`

**Interfaces:**
- Extends `cbind_decode_scalar()` to accept the complete D2 numeric token sets.
- Adds private helpers:

```c
bool cbind_u64_exact_in_binary(uint64_t magnitude, unsigned precision_bits);
bool cbind_s64_exact_in_binary(int64_t value, unsigned precision_bits);
bool cbind_double_is_integral(double value);
bool cbind_double_fits_signed_bits(double value, unsigned bits);
bool cbind_double_fits_unsigned_bits(double value, unsigned bits);
```

These remain private in `internal.h` or `scalar.c`; no numeric policy becomes public ABI.

- [ ] **Step 1: Add numeric-boundary RED tests**

Add concrete tests:

```text
FLOAT 42.0 -> int/long                  OK
FLOAT 42.5 -> int                       VALUE_OUT_OF_RANGE
FLOAT NaN/+Inf/-Inf -> integer          VALUE_OUT_OF_RANGE
FLOAT exactly signed lower bound        OK
FLOAT signed upper-exclusive bound      VALUE_OUT_OF_RANGE
FLOAT -1.0 -> size_t                    VALUE_OUT_OF_RANGE
FLOAT 0.0 -> size_t                     OK
FLOAT -> double NaN/Inf                 OK and preserved by isnan/isinf/signbit as applicable
FLOAT DBL_MAX -> float                  VALUE_OUT_OF_RANGE
FLOAT smallest positive double -> float VALUE_OUT_OF_RANGE when cast becomes 0
FLOAT 1.0 + DBL_EPSILON -> float        OK; normal finite rounding allowed
SINT 2^24 -> float                      OK
SINT 2^24+1 -> float                    VALUE_OUT_OF_RANGE
UINT 2^24 -> float                      OK
UINT 2^24+1 -> float                    VALUE_OUT_OF_RANGE
SINT/UINT 2^53 -> double                OK
SINT/UINT 2^53+1 -> double              VALUE_OUT_OF_RANGE
```

Use `ldexp(1.0, n)`/integer shifts guarded by widths instead of decimal magic numbers where that makes the boundary self-evident. Use `<float.h>`, `<math.h>`, `<limits.h>`, and `<stdint.h>`.

For 64-bit integer exactness, include `UINT64_MAX` and `INT64_MIN` paths so magnitude computation never relies on `-INT64_MIN`.

- [ ] **Step 2: Run numeric RED and commit tests**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
```

Expected: new FLOAT/integer-to-float tests FAIL with `CBIND_TOKEN_MISMATCH` or missing conversion behavior, while Task 2 cases remain PASS.

```bash
git add cbind/tests/cbind_scalar_decode_test.c
git commit -m "test(cbind): specify strict numeric conversion"
```

- [ ] **Step 3: Implement safe double-to-integer proof**

Never cast until finite/integral/range checks pass.

Integral proof:

```c
bool cbind_double_is_integral(double value) {
    double integral;
    return isfinite(value) && modf(value, &integral) == 0.0;
}
```

For signed N-bit two's-complement storage used by supported targets, prove the exact half-open mathematical range with powers of two:

```text
-2^(N-1) <= value < 2^(N-1)
```

For unsigned N-bit:

```text
0 <= value < 2^N
```

Compute powers with `ldexp(1.0, exponent)`, which exactly represents these binary boundaries and avoids the `double(INT64_MAX)` rounding trap. Then narrow only after proof.

Use the actual target width established by the canonical CMeta descriptor (`sizeof(int/long/size_t) * CHAR_BIT`).

- [ ] **Step 4: Implement exact integer-to-binary-float proof without unsafe round-trip casts**

Compute signed magnitude safely:

```c
uint64_t magnitude;
if (value < 0)
    magnitude = (uint64_t)(-(value + 1)) + 1u;
else
    magnitude = (uint64_t)value;
```

Exactness helper:

```c
bool cbind_u64_exact_in_binary(uint64_t magnitude, unsigned precision_bits) {
    unsigned width = 0u;
    uint64_t tmp = magnitude;
    unsigned discarded;
    uint64_t mask;

    if (magnitude == 0u)
        return true;
    while (tmp != 0u) {
        ++width;
        tmp >>= 1u;
    }
    if (width <= precision_bits)
        return true;
    discarded = width - precision_bits;
    mask = (UINT64_C(1) << discarded) - UINT64_C(1);
    return (magnitude & mask) == 0u;
}
```

For D2 64-bit integer inputs, `discarded` is at most 40 for binary32 and 11 for binary64, so this shift is bounded. Call with `FLT_MANT_DIG` or `DBL_MANT_DIG` and only cast to float/double after exactness is proven.

- [ ] **Step 5: Implement binary64-to-binary32 overflow/underflow policy**

Pattern:

```c
if (isnan(source) || isinf(source)) {
    float narrowed = (float)source;
    memcpy(out, &narrowed, sizeof(narrowed));
    return CBIND_OK;
}

{
    float narrowed = (float)source;
    if (isinf(narrowed))
        return cbind_scalar_range_error(...);
    if (source != 0.0 && narrowed == 0.0f)
        return cbind_scalar_range_error(...);
    memcpy(out, &narrowed, sizeof(narrowed));
    return CBIND_OK;
}
```

Finite precision rounding inside the representable nonzero range is accepted by design.

- [ ] **Step 6: Run numeric GREEN on scalar suite**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
```

Expected: PASS with exact `2^24/2^24+1` and `2^53/2^53+1` distinctions.

Run UBSan/ASan preset if the repository's current preset set exposes one; if no sanitizer preset exists, do not invent workflow-local flags. The essential verification is that no rejected conversion performs an out-of-range C cast before proof.

- [ ] **Step 7: Commit Task 3 GREEN**

```bash
git add cbind
git commit -m "feat(cbind): enforce exact numeric conversion"
```

---

### Task 4: Complete recursive semantic/storage preflight, cycle safety, depth, and exact scratch budget

**Files:**
- Create: `cbind/src/struct.c`
- Create: `cbind/tests/cbind_struct_decode_test.c`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/CMakeLists.txt`
- Modify: `cbind/tests/CMakeLists.txt`

**Interfaces:**
- Completes `cbind_validate_graph()` for STRUCT.
- Produces private struct helpers:

```c
size_t cbind_bitmap_bytes(size_t field_count);
const cmeta_data_field_desc *cbind_find_field_slice(
    const cmeta_data_struct_shape *shape,
    const cserde_slice *key,
    size_t *index);

cbind_status cbind_decode_struct(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *parent_field,
    size_t depth,
    void *out);
```

`cbind_decode_struct()` is implemented fully in Task 5; Task 4 establishes its declaration and complete preflight/resource proof.

- [ ] **Step 1: Register struct test target and define reusable semantic test models**

Add to `cbind/tests/CMakeLists.txt`:

```cmake
cmake_add_test(cbind_struct_decode_test
  SOURCES cbind_struct_decode_test.c
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

set_target_properties(cbind_struct_decode_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

In `cbind_struct_decode_test.c`, define reflected/storage/semantic descriptors explicitly using the same CMeta pattern as `cmeta/tests/cmeta_data_test.c`:

```c
Struct(cbind_test_inner,
    (int, count),
    (double, ratio)
);

Struct(cbind_test_record,
    (int, id),
    (long, score),
    (cbind_test_inner, inner),
    (int, untouched)
);
```

Because `Struct(...)` cannot infer a semantic descriptor for the custom nested struct automatically, define canonical `cmeta_type_identity`, `cmeta_type_desc`, `cmeta_data_field_desc[]`, `cmeta_data_struct_shape`, and `cmeta_data_desc` objects for `cbind_test_inner` and `cbind_test_record`. Include only `id`, `score`, and `inner` in the outer semantic field array; `untouched` is reflected-but-nonsemantic.

- [ ] **Step 2: Write preflight RED tests with zero provider consumption**

Every case initializes a recording reader and asserts `source.index == 0u` after failure.

Required cases:

```text
valid scalar root max_depth=0, scratch=0                       accepted (existing scalar suite)
root empty struct max_depth=0                                  LIMIT_EXCEEDED
root empty struct max_depth=1, scratch=0                       reaches runtime decode path
one-field root struct scratch=0                                LIMIT_EXCEEDED
one-field root struct scratch=1                                reaches runtime decode path
8-field root scratch=1                                         reaches runtime decode path
9-field root scratch=1                                         LIMIT_EXCEEDED
9-field root scratch=2                                         reaches runtime decode path
nested path root(1 byte)+inner(1 byte), scratch=1               LIMIT_EXCEEDED
same nested path scratch=2                                     reaches runtime decode path
sibling nested structs                                         max active-path sum, not schema-total sum
struct storage size mismatch                                   INVALID_SHAPE
struct storage alignment mismatch                              INVALID_SHAPE
semantic/reflected offset mismatch                             INVALID_SHAPE
semantic child storage size mismatch                           INVALID_SHAPE
semantic child storage alignment mismatch                      INVALID_SHAPE
duplicate semantic field name/mapping                          INVALID_SHAPE
semantic field offset + child size overflow/out of parent      INVALID_SHAPE
self-referential semantic descriptor cycle                     INVALID_SHAPE
nested valid-but-unsupported child                             UNSUPPORTED
```

For cycle construction use local mutable descriptor objects so no linker trick is needed:

```c
cmeta_data_desc cycle_desc = valid_record_desc;
cmeta_data_field_desc cycle_field = valid_record_fields[0];
cmeta_data_struct_shape cycle_shape = valid_record_shape;

cycle_field.value = &cycle_desc;
cycle_shape.fields = &cycle_field;
cycle_shape.field_count = 1u;
cycle_desc.shape = &cycle_shape;
```

- [ ] **Step 3: Run preflight RED and commit tests**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_struct_decode_test$' --output-on-failure
```

Expected: new struct preflight cases FAIL because STRUCT graph/resource proof is not yet implemented. Existing scalar suite remains PASS.

```bash
git add cbind/tests cbind/CMakeLists.txt
git commit -m "test(cbind): specify struct preflight budgets"
```

- [ ] **Step 4: Implement overflow-safe bitmap and DFS cycle/resource proof**

Bitmap helper:

```c
size_t cbind_bitmap_bytes(size_t field_count) {
    return field_count / 8u + (field_count % 8u != 0u ? 1u : 0u);
}
```

DFS frame:

```c
typedef struct cbind_validation_frame {
    const cmeta_data_desc *shape;
    const struct cbind_validation_frame *parent;
} cbind_validation_frame;
```

Before entering a STRUCT, scan `parent` chain for `shape` address equality; an active repeat is `CBIND_INVALID_SHAPE`. This address comparison is cycle detection only; canonical scalar storage matching still uses `cmeta_type_equal()`.

Compute depth and scratch before recursion:

```text
next_depth = depth + 1
if next_depth > context->max_depth -> LIMIT_EXCEEDED
frame_bytes = cbind_bitmap_bytes(field_count)
if active_scratch > SIZE_MAX - frame_bytes -> LIMIT_EXCEEDED
next_active = active_scratch + frame_bytes
*max_scratch = max(*max_scratch, next_active)
```

Then recurse each semantic child with the same frame and `next_active`. Siblings reuse the same `active_scratch` base because this is path maximum, not cumulative schema total.

- [ ] **Step 5: Implement complete struct storage proof**

For each STRUCT:

```text
cmeta_data_desc_valid(desc)
shape/layout/storage_type non-NULL
storage_type->size  == layout->size
storage_type->align == layout->align
```

For each semantic field, use `cmeta_struct_find_field(layout, field->name)` and prove:

```text
name unique among semantic fields
resolved reflected field exists
semantic offset == reflected offset
reflected field size == child storage_type->size
reflected field align == child storage_type->align
field->offset <= parent size
child_size <= parent size - field->offset
no second semantic field resolves to same reflected field
child graph recursively valid/supported
```

Use subtraction form for bounds; do not compute unchecked `offset + child_size`.

- [ ] **Step 6: Run preflight GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_(scalar|struct)_decode_test$' --output-on-failure
```

Expected: preflight/resource tests PASS. Valid struct inputs may still fail at runtime until Task 5, but the RED cases in this task must distinguish “preflight accepted and reader was touched” from “preflight rejected at source.index==0”. Do not assert successful struct decode until Task 5.

- [ ] **Step 7: Commit Task 4 GREEN**

```bash
git add cbind
git commit -m "feat(cbind): preflight struct storage and budgets"
```

---

### Task 5: Strict struct MAP decoder, nested recursion, borrowed key dispatch, and scratch rewind

**Files:**
- Modify: `cbind/src/decode.c`
- Modify: `cbind/src/struct.c`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/tests/cbind_struct_decode_test.c`

**Interfaces:**
- Produces private generic recursive runtime dispatch:

```c
cbind_status cbind_decode_value(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    void *out);
```

- `cbind_decode_value()` routes BOOL/SINT/UINT/FLOAT to `cbind_decode_scalar()` and STRUCT to `cbind_decode_struct()`. Unsupported kinds can never reach runtime after successful preflight.
- `cbind_decode()` calls `cbind_decode_value(..., field=NULL, depth=0, out)` after preflight and empty-state checks.

- [ ] **Step 1: Write strict struct decoding RED cases**

Concrete canonical token arrays must cover:

```text
empty semantic struct: MAP_BEGIN MAP_END                              OK
flat struct descriptor order                                         OK
flat struct reversed input order                                     OK
nested struct                                                        OK
case-sensitive exact field name                                      OK only exact case
unknown field                                                        UNKNOWN_FIELD
same known field twice                                               DUPLICATE_FIELD
MAP_END before every required field                                  MISSING_FIELD
UINT or other non-STRING map key                                     TOKEN_MISMATCH
ARRAY_BEGIN as root for struct                                       TOKEN_MISMATCH
known field followed by wrong scalar token                           TOKEN_MISMATCH
```

Transient non-NUL key test must use a byte array without trailing NUL:

```c
static const unsigned char id_key_bytes[] = { 'i', 'd' };
const cserde_token key = {
    .kind = CSERDE_STRING,
    .value.slice = {
        id_key_bytes,
        sizeof(id_key_bytes),
        CSERDE_VIEW_TRANSIENT
    }
};
```

Do not call `cmeta_data_struct_find_field()` with this borrowed slice because that helper requires a C string.

Add a scratch-reuse success case with two sibling nested structs where the total schema bitmap bytes exceed scratch but the maximum simultaneously active path fits; decoding must succeed.

- [ ] **Step 2: Run struct runtime RED and commit tests**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_struct_decode_test$' --output-on-failure
```

Expected: preflight cases remain PASS; successful/strict runtime struct cases FAIL because struct MAP consumption is incomplete.

```bash
git add cbind/tests/cbind_struct_decode_test.c
git commit -m "test(cbind): specify strict struct map decode"
```

- [ ] **Step 3: Implement length-delimited field lookup**

`cbind_find_field_slice()`:

```c
const cmeta_data_field_desc *cbind_find_field_slice(
    const cmeta_data_struct_shape *shape,
    const cserde_slice *key,
    size_t *index) {
    size_t i;

    if (shape == NULL || key == NULL)
        return NULL;
    for (i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        size_t name_size = strlen(field->name);
        if (name_size == key->size &&
            (key->size == 0u || memcmp(key->data, field->name, key->size) == 0)) {
            if (index != NULL)
                *index = i;
            return field;
        }
    }
    return NULL;
}
```

The function neither allocates nor stores `key->data`.

- [ ] **Step 4: Implement struct scratch-frame ownership and seen bitmap**

At struct entry:

```text
require MAP_BEGIN
frame_bytes = cbind_bitmap_bytes(field_count)
mark = state->scratch_used
assert defensive bounds: frame_bytes <= scratch_size - mark
bitmap = state->scratch + mark
memset(bitmap, 0, frame_bytes)
state->scratch_used = mark + frame_bytes
```

On every exit path from this struct, restore:

```c
state->scratch_used = mark;
```

Seen helpers are byte operations only:

```c
static bool cbind_seen(const unsigned char *bitmap, size_t index) {
    return (bitmap[index / 8u] & (unsigned char)(1u << (index % 8u))) != 0u;
}

static void cbind_mark_seen(unsigned char *bitmap, size_t index) {
    bitmap[index / 8u] |= (unsigned char)(1u << (index % 8u));
}
```

Zero-field struct with zero scratch must not dereference a NULL scratch pointer; `memset` is called only when `frame_bytes != 0u`.

- [ ] **Step 5: Implement normative struct state machine**

After MAP_BEGIN, loop on `cbind_read_required()`:

```text
MAP_END:
  scan field indices in descriptor order
  first unseen -> CBIND_MISSING_FIELD with that canonical field
  none unseen  -> success

STRING key:
  exact borrowed-slice lookup
  no field -> CBIND_UNKNOWN_FIELD, field=NULL, value remains unconsumed
  already seen -> CBIND_DUPLICATE_FIELD, duplicate value remains unconsumed
  resolve child pointer as (unsigned char *)out + field->offset
  cbind_decode_value(child, field, current struct depth, child_out)
  only after child success mark seen

anything else:
  CBIND_TOKEN_MISMATCH, paired value remains unconsumed
```

Depth attribution: entering a struct increments depth before interpreting that struct. Thus root struct errors have depth `1`; scalar field errors in root use depth `1`; nested struct errors use `2+`.

- [ ] **Step 6: Run struct GREEN and verify reader positions**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_struct_decode_test$' --output-on-failure
```

Expected: PASS.

Explicitly assert recording `source.index`:

```text
unknown field    -> MAP_BEGIN + key consumed, unknown value not consumed
duplicate field  -> duplicate key consumed, duplicate value not consumed
non-string key   -> key token consumed, paired value not consumed
```

- [ ] **Step 7: Commit Task 5 GREEN**

```bash
git add cbind
git commit -m "feat(cbind): decode strict semantic structs"
```

---

### Task 6: Transaction rollback, source failures, deterministic error attribution, and nonsemantic preservation

**Files:**
- Modify: `cbind/src/decode.c`
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/src/struct.c`
- Modify: `cbind/tests/cbind_scalar_decode_test.c`
- Modify: `cbind/tests/cbind_struct_decode_test.c`

**Interfaces:**
- Finalizes `cbind_value_reset()` recursively for the complete D2 supported graph.
- Finalizes `cbind_error_set()` usage so every failure site has deterministic `status/source_status/shape/field/depth`.
- No new public API.

- [ ] **Step 1: Add rollback and source RED tests**

Add a test-local failure reader provider only for status injection; do not duplicate the recording provider:

```c
typedef struct failing_reader_context {
    const cserde_token *prefix;
    size_t prefix_count;
    size_t index;
    size_t calls;
    cserde_status failure;
} failing_reader_context;

static cserde_status failing_reader_next(void *context, cserde_token *out) {
    failing_reader_context *state = (failing_reader_context *)context;
    ++state->calls;
    if (state->index < state->prefix_count) {
        *out = state->prefix[state->index++];
        return CSERDE_OK;
    }
    return state->failure;
}
```

Required tests:

```text
empty input for scalar                           UNEXPECTED_END / source DONE / depth 0
empty input for struct                           UNEXPECTED_END / source DONE / depth 1
mid-struct DONE while key required               UNEXPECTED_END / source DONE
mid-struct DONE while known value required       UNEXPECTED_END / source DONE + known field
provider SOURCE_ERROR                            SOURCE_ERROR / exact SOURCE_ERROR
provider VALUE_OUT_OF_RANGE                      SOURCE_ERROR / exact VALUE_OUT_OF_RANGE
provider CALLBACK_ERROR sticky result            SOURCE_ERROR / exact CALLBACK_ERROR
failure after first successful field             root semantic fields all reset
nested child failure                             entire root semantic graph reset
wrong second field token                         earlier writes reset
nonsemantic reflected field on success           unchanged
nonsemantic reflected field on rollback          unchanged
no provider call after reported failure          calls exact at failure boundary
```

Error-site assertions must include:

```text
root scalar mismatch:       shape=root scalar, field=NULL, depth=0
known root field mismatch:  shape=child scalar, field=&root_field, depth=1
unknown root field:         shape=root struct, field=NULL, depth=1
duplicate root field:       shape=root struct, field=&duplicated_field, depth=1
missing nested field:       shape=nested struct, field=&first_missing_nested_field, depth=2
```

- [ ] **Step 2: Run transaction/source RED and commit tests**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_(scalar|struct)_decode_test$' --output-on-failure
```

Expected: at least rollback/error-attribution/source-prefix cases FAIL; previous success cases remain PASS.

```bash
git add cbind/tests
git commit -m "test(cbind): specify transactional failure semantics"
```

- [ ] **Step 3: Finalize recursive semantic reset**

`cbind_value_reset()` behavior:

```text
BOOL        typed bool false + memcpy
SINT        typed int/long zero + memcpy according to proven canonical storage
UINT        typed size_t zero + memcpy
FLOAT       typed float/double +0 + memcpy
STRUCT      recurse semantic fields only using field offsets
```

Never `memset(out, 0, storage_type->size)` for structs. Never touch reflected fields absent from semantic shape.

`cbind_decode()` owns the root transaction:

```c
status = cbind_decode_value(&state, shape, NULL, 0u, out);
if (status != CBIND_OK)
    cbind_value_reset(shape, out);
else
    cbind_error_clear(error);
return status;
```

Child decoders do not independently roll back the whole root; they report the most specific failure and let the root transaction reset once.

- [ ] **Step 4: Make error attribution monotonic toward the most specific child**

Rule: once a child decode has populated an error with a deeper/more specific `shape/depth`, outer frames return the status unchanged and do not overwrite it. Struct-level failures generated locally set the current struct shape/depth and the field rules from the spec.

A known child call receives the canonical parent field pointer:

```c
status = cbind_decode_value(state,
                            field->value,
                            field,
                            current_depth,
                            child_out);
if (status != CBIND_OK)
    goto fail;
```

Unknown/non-string current keys pass `field=NULL`.

- [ ] **Step 5: Run complete D2 functional GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_(scalar|struct)_decode_test$' --output-on-failure
ctest --preset test-release-linux -R '^(cmeta_|cserde_|cbind_)' --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit Task 6 GREEN**

```bash
git add cbind
git commit -m "feat(cbind): guarantee transactional decode failures"
```

---

### Task 7: C++17 linkage, dependency/allocation audit, installed export, and exact-head Linux/Windows conformance

**Files:**
- Create: `cbind/tests/cbind_header_cpp_test.cpp`
- Modify: `cbind/tests/CMakeLists.txt`
- Modify if needed after export verification: `cbind/CMakeLists.txt`
- Modify if needed after workflow verification: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: complete Tasks 1-6 public API.
- Produces: no new binding semantics; proves public ABI/package/build constraints.

- [ ] **Step 1: Add C++17 RED/linkage test**

`cbind/tests/cbind_header_cpp_test.cpp`:

```cpp
#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<cbind_context>::value,
              "context ABI");
static_assert(std::is_standard_layout<cbind_error>::value,
              "error ABI");
static_assert(CBIND_CONTEXT_ABI_VERSION == 1u, "context ABI version");
static_assert(CBIND_ERROR_ABI_VERSION == 1u, "error ABI version");

spec("CBind C++17 public linkage") {
  it("calls the C decoder from C++") {
    const cserde_token tokens[] = {
        { CSERDE_SINT, { .sint = 7 } }
    };
    cserde_recording_reader_context source = { tokens, 1u, 0u };
    cserde_reader reader{};
    cbind_context context = CBIND_CONTEXT_INIT(nullptr, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, &error),
                CBIND_OK);
    check_equal(out, 7);
  }
}
```

If designated union initialization above is rejected by the repository's C++17 compiler mode, initialize the token field-by-field instead; do not change the public C structs to accommodate the test.

Register:

```cmake
cmake_add_test(cbind_header_cpp_test
  SOURCES cbind_header_cpp_test.cpp
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

set_target_properties(cbind_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
```

- [ ] **Step 2: Run C++ test and fix only public C/C++ compatibility defects**

```bash
cmake --build --preset build-default-linux --target cbind_header_cpp_test
ctest --preset test-release-linux -R '^cbind_header_cpp_test$' --output-on-failure
```

Expected final result: PASS. Any fix must preserve natural `cbind_*` C ABI and standard `extern "C"` declarations; do not add C++ wrapper classes.

- [ ] **Step 3: Audit forbidden dependencies, includes, allocation, and duplicate test support**

Run:

```bash
rg -n "TurboUtils::(STL|Core|CFlow)|TurboParser|turbostl|cflow/|turbo_parser|parser/" cbind
rg -n "\b(malloc|calloc|realloc|free)\s*\(" cbind/src cbind/include
rg -n "cserde_recording_reader_ops|cserde_recording_reader_context" cbind/tests cserde/tests/support
```

Expected:

```text
first command: no production CBind dependency/include matches (test prose is not production)
second command: no matches
third command: definitions exist only in cserde/tests/support; CBind only references them
```

Inspect `cbind/CMakeLists.txt` and confirm the only production link dependencies are:

```cmake
target_link_libraries(turbo_cbind
  PUBLIC TurboUtils::CMeta TurboUtils::CSerde)
```

- [ ] **Step 4: Verify installed export surface from a clean Linux build**

Use a clean staging prefix; do not infer install success from build-tree aliases:

```bash
rm -rf build/release-linux-ninja /tmp/turboutils-cbind-install /tmp/cbind-consumer
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
cmake --install build/release-linux-ninja --prefix /tmp/turboutils-cbind-install
```

Locate the installed target file and confirm it exports `TurboUtils::CBind`/`CBind` and references only CMeta/CSerde transitively for CBind.

Create a temporary external consumer:

```bash
mkdir -p /tmp/cbind-consumer
cat > /tmp/cbind-consumer/CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(CBindConsumer C)
find_package(TurboUtils CONFIG REQUIRED)
add_executable(cbind_consumer main.c)
target_link_libraries(cbind_consumer PRIVATE TurboUtils::CBind)
EOF
cat > /tmp/cbind-consumer/main.c <<'EOF'
#include <cbind/cbind.h>
int main(void) {
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    return context.abi_version == 1u && error.abi_version == 1u ? 0 : 1;
}
EOF
cmake -S /tmp/cbind-consumer -B /tmp/cbind-consumer/build \
  -DCMAKE_PREFIX_PATH=/tmp/turboutils-cbind-install
cmake --build /tmp/cbind-consumer/build
/tmp/cbind-consumer/build/cbind_consumer
```

Expected: configure/build/run PASS without adding TurboSTL/CFlow/Core/parser targets explicitly.

- [ ] **Step 5: Run fresh exact-head Linux conformance**

From the exact final candidate head:

```bash
rm -rf build/release-linux-ninja
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux --output-on-failure
ctest --preset test-release-linux -R '^(cmeta_|cserde_|cbind_|cflow_|turbostl_)' --output-on-failure
```

Expected: full CTest PASS and selected conformance PASS. Record exact head SHA and counts.

- [ ] **Step 6: Push exact head and require Windows workflow on the same SHA**

The workflow already uses repository presets and `VsDevCmd`; after the `cbind/**` path/regex update it must execute:

```text
cmake --preset release-win-msvc-ninja
cmake --build --preset build-release-windows
ctest --preset test-release-windows -R "^(cmeta_|cserde_|cbind_|cflow_|turbostl_)" --output-on-failure
```

Do not add workflow-local compiler flags or duplicate preset configuration.

Expected on the same exact head SHA:

```text
Linux release   configure + build + full/selected tests PASS
Windows release configure + build + selected tests PASS
```

If Windows exposes a `long`/size boundary failure, fix the implementation/tests using `<limits.h>`/`sizeof` facts rather than weakening the contract or assuming LP64.

- [ ] **Step 7: Commit final compatibility/CI changes**

```bash
git add cbind .github/workflows/cmeta.yml CMakeLists.txt
git commit -m "test(cbind): enforce public conformance gates"
```

If Step 7 produces a new head, rerun/reconfirm Steps 5-6 on that new exact head before calling the implementation complete.

---

## Final Review Checklist

Before requesting code review, verify every item explicitly:

```text
[ ] TurboUtils::CBind exists and exports/install correctly.
[ ] CBind production links only CMeta + CSerde.
[ ] Public API remains only context/error/decode D2 surface; no parser/CFlow/incremental ABI leaked in.
[ ] All unsupported semantic kinds fail before provider consumption.
[ ] All malformed shape/storage graphs fail before provider consumption.
[ ] All scratch/depth failures fail before provider consumption.
[ ] Non-empty semantic destination fails before provider consumption.
[ ] BOOL/int/long/size_t/float/double storage is proven canonically.
[ ] Numeric boundaries and exact integer-to-float rules pass.
[ ] Struct MAP keys are exact STRING slices and need no NUL/copy.
[ ] Unknown/duplicate/missing/non-string keys follow strict D2 semantics.
[ ] Scratch uses active-path maximum and sibling reuse; no field-count cap.
[ ] Post-consumption failures reset the full semantic root only.
[ ] Padding and nonsemantic reflected fields remain untouched.
[ ] Reader is not rewound/skipped after failure.
[ ] DONE vs source failures preserve distinct CBind/CSerde status.
[ ] Transient key pointers never escape dispatch.
[ ] No production heap allocation exists.
[ ] CSerde recording support is reused, not duplicated.
[ ] C11 and C++17 consumers compile/link.
[ ] Fresh installed-package consumer links `TurboUtils::CBind`.
[ ] Exact same final head passes Linux and Windows conformance.
```

Only after this checklist and exact-head CI pass should the branch proceed to code review/integration choice; merge remains an explicit user decision.
