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
- Execution starts from the latest `master`, not historical design base `4216bb8c71cf6564bf88fe6cff3c8a1c227c87d3`. At plan-writing time current master is `5972c4ee986d54befebbc3b4dcb535082a9286cd`; if master advances, use the newer exact head.
- Before Task 1 execution, create an isolated worktree/feature branch `feat/cbind-scalar-struct-decode` from that latest master, then cherry-pick approved docs commits `9d47be11fb2a27a50e6928212c6b172a7761eeb8`, `fa456eb00cd3fcf9672e816b6961ff99f95035f5`, and the commit containing this plan so the spec travels with implementation. Do not base production work on the historical design branch.
- Production dependency is exactly `TurboUtils::CMeta + TurboUtils::CSerde`. No production CBind file may include/link TurboSTL, Core/utils, CFlow, TurboParser, or concrete parser code.
- D2 public API is context-first and decode-only: `cbind_decode(ctx, shape, reader, out, error)`.
- Supported semantic kinds are exactly `CMETA_DATA_BOOL`, `CMETA_DATA_SINT`, `CMETA_DATA_UINT`, `CMETA_DATA_FLOAT`, and `CMETA_DATA_STRUCT`.
- Valid STRING/BYTES/ENUM/VARIANT/SEQUENCE/SET/MAP/CUSTOM descriptors return `CBIND_UNSUPPORTED` before reader consumption.
- Canonical writable scalar storage is limited to CMeta built-ins: bool; int/long; size_t; float/double. Match canonical storage identity with `cmeta_type_equal`, then separately prove exact size/alignment/semantic width; raw descriptor-pointer equality is not the storage-identity contract.
- Required pointer ordering is fixed: `shape == NULL`, `reader == NULL`, or `out == NULL` returns `CBIND_INVALID_ARGUMENT`; a non-NULL malformed `cbind_error` returns `CBIND_INVALID_ARGUMENT`; a NULL/malformed `cbind_context` returns `CBIND_INVALID_CONTEXT`.
- All validation/resource/destination-empty failures happen before the first `cserde_reader_next()` call and leave reader/provider state and destination unchanged.
- Struct depth semantics are scalar root `0`, root struct `1`, nested struct `2+`.
- Scratch is caller-owned bookkeeping only. Per active struct bitmap bytes are computed overflow-safely as `field_count / 8u + (field_count % 8u != 0u)`; do not evaluate `(field_count + 7u) / 8u` without an overflow proof.
- No fixed field-count limit and no production heap allocation. D2 production files do not call `malloc`, `calloc`, `realloc`, or `free`.
- Decode failure after consumption restores every supported semantic field of the root destination to the D2 empty state; padding and reflected-but-nonsemantic fields remain untouched. Reader position is never rewound and no remainder is silently skipped.
- CSerde `DONE` while a token is required maps to `CBIND_UNEXPECTED_END` with `source_status=CSERDE_DONE`; every other non-OK reader result maps to `CBIND_SOURCE_ERROR` with the exact returned `cserde_status`.
- CSerde recording support remains BUILD_TESTS-only and is reused by CBind tests through `cserde_recording_support`; do not copy/fork a second recording token source into CBind.
- Raw `cserde_token` is structural transport, not a CFlow business stream. D2 adds no CFlow or parser integration and no public `begin/feed/finish` decoder ABI.
- Public headers compile/link in C11 and C++17; no public `_Generic` or GNU-only declarations.
- Repository Linux release preset writes to `build/linux-gcc-release`; Windows release preset writes to `build/Msvc-Release`.
- Final exact implementation head passes fresh Linux and Windows release configure/build/test with `cbind_*` included in `.github/workflows/cmeta.yml`.

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

`cbind/tests` reuses global BUILD_TESTS-only target `cserde_recording_support` created by `cserde/tests/CMakeLists.txt`; root module order keeps `cserde` before `cbind`.

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

- Produces internal ABI helpers:

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

- [ ] **Step 1: Add module/test scaffold and ABI declarations, omitting initializer macros for the first RED**

`cbind/CMakeLists.txt`:

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

Root order:

```cmake
add_subdirectory(cmeta)
add_subdirectory(cserde)
add_subdirectory(cbind)
add_subdirectory(cflow)
```

Initial test registration:

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

Declare enum/struct/function types, but omit `CBIND_CONTEXT_INIT` and `CBIND_ERROR_INIT`. `src/decode.c` can contain ABI helper definitions; `cbind_decode` need not be defined until Task 2 because Task 1 does not call it.

Modify `.github/workflows/cmeta.yml` in both PR/push path filters:

```yaml
      - "cbind/**"
```

Change both selected-test regexes to:

```text
^(cmeta_|cserde_|cbind_|cflow_|turbostl_)
```

Rename Linux selected step to `Test CMeta, CSerde, CBind, CFlow, and TurboSTL`.

- [ ] **Step 2: Write ABI RED tests**

```c
#include <cbind/cbind.h>
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

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

Also assert `CBIND_OK == 0`, `CBIND_CONTEXT_ABI_VERSION == 1u`, and `CBIND_ERROR_ABI_VERSION == 1u`.

- [ ] **Step 3: Run RED and commit it**

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
```

Expected: compile fails only because the two initializer macros are undefined.

```bash
git add CMakeLists.txt .github/workflows/cmeta.yml cbind
git commit -m "test(cbind): define public ABI contract"
```

- [ ] **Step 4: Add header-side initializers and ABI helper implementation**

Add the exact macros above. `decode.h` wraps `cbind_decode` in standard `extern "C"`; `cbind.h` includes all CBind public headers.

`decode.c` implements:

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

`cbind_error_clear()` normalizes v1 fields to OK/NULL/0. `cbind_error_set()` is NULL-safe and writes only v1 diagnostic fields.

- [ ] **Step 5: Run GREEN and dependency audit**

```bash
cmake --build --preset build-default-linux --target turbo_cbind cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
rg -n "target_link_libraries|TurboUtils::(STL|Core|CFlow)|TurboParser" cbind CMakeLists.txt
```

Expected: test PASS; production CBind link line contains only CMeta/CSerde; no forbidden production dependency/include appears.

- [ ] **Step 6: Commit Task 1 GREEN**

```bash
git add cbind CMakeLists.txt .github/workflows/cmeta.yml
git commit -m "feat(cbind): add public ABI shell"
```

---

### Task 2: Scalar preflight, destination-empty proof, reader mapping, BOOL and integer decode

**Files:**
- Create: `cbind/src/scalar.c`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/CMakeLists.txt`
- Modify: `cbind/tests/cbind_scalar_decode_test.c`

**Interfaces:**

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

Task 2 completes BOOL/SINT/UINT scalar roots. FLOAT is completed in Task 3; STRUCT graph/runtime is completed in Tasks 4-5.

- [ ] **Step 1: Add recording-reader helper and scalar RED cases**

```c
#include "recording.h"

static void init_recording_reader(cserde_reader *reader,
                                  cserde_recording_reader_context *context,
                                  const cserde_token *tokens,
                                  size_t count) {
    *reader = (cserde_reader){0};
    *context = (cserde_recording_reader_context){tokens, count, 0u};
    check_equal(cserde_reader_init(reader, &cserde_recording_reader_ops, context),
                CSERDE_OK);
}
```

Required concrete cases:

```text
BOOL <- BOOL true/false                         OK
BOOL <- SINT                                    TOKEN_MISMATCH
int  <- SINT(INT_MIN/INT_MAX)                   OK
long <- SINT(LONG_MIN/LONG_MAX)                 OK using platform limits
int  <- UINT((uint64_t)INT_MAX)                 OK
int  <- UINT((uint64_t)INT_MAX + 1u)            VALUE_OUT_OF_RANGE
size <- UINT(SIZE_MAX)                          OK
size <- SINT(-1)                                VALUE_OUT_OF_RANGE
STRING token -> int                             TOKEN_MISMATCH
```

Pre-consumption cases all assert recording `source.index == 0u`:

```text
shape == NULL / reader == NULL / out == NULL   INVALID_ARGUMENT
malformed non-NULL error prefix                 INVALID_ARGUMENT
context == NULL                                INVALID_CONTEXT
context prefix one byte short                  INVALID_CONTEXT
context wrong ABI                              INVALID_CONTEXT
scratch_size > 0 with scratch == NULL          INVALID_CONTEXT
valid STRING semantic descriptor               UNSUPPORTED
canonical int descriptor with forged width     INVALID_SHAPE
valid noncanonical integer storage identity    UNSUPPORTED
non-empty int destination                      DESTINATION_NOT_EMPTY
```

For `SIZE_MAX`, only build a canonical UINT token when `(uintmax_t)SIZE_MAX <= UINT64_MAX`; this is true on current Linux/Windows 64-bit targets and keeps the test tied to the CSerde `uint64_t` wire contract.

- [ ] **Step 2: Run scalar RED and commit tests**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
```

Expected: link fails on `cbind_decode`, or the new behavior tests fail if a definition was introduced while arranging Task 1. Existing CMeta/CSerde targets must still build.

```bash
git add cbind
git commit -m "test(cbind): specify scalar decode preflight"
```

- [ ] **Step 3: Implement public validation/preflight orchestration and reader mapping**

`cbind_decode()` ordering:

```c
if (shape == NULL || reader == NULL || out == NULL)
    return CBIND_INVALID_ARGUMENT;
if (error != NULL && !cbind_error_valid(error))
    return CBIND_INVALID_ARGUMENT;
if (!cbind_context_valid(context)) {
    cbind_error_set(error, CBIND_INVALID_CONTEXT, CSERDE_OK,
                    shape, NULL, 0u);
    return CBIND_INVALID_CONTEXT;
}
```

Then graph/resource preflight, destination-empty check, decode, and root rollback. No reader call appears above successful preflight/empty proof.

`cbind_read_required()` is the sole source mapping helper:

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

- [ ] **Step 4: Implement canonical scalar storage proof and typed `memcpy` access**

Identity helper:

```c
static bool cbind_type_matches(const cmeta_type_desc *actual,
                               const cmeta_type_desc *canonical) {
    return actual != NULL && canonical != NULL &&
           cmeta_type_equal(actual, canonical) &&
           actual->size == canonical->size &&
           actual->align == canonical->align;
}
```

BOOL accepts canonical bool. SINT accepts canonical int/long. UINT accepts canonical size_t. FLOAT accepts canonical float/double. SINT/UINT/FLOAT require semantic bits equal `storage_type->size * CHAR_BIT`.

Read/write/reset erased storage only through typed locals plus `memcpy`:

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

- [ ] **Step 5: Implement BOOL and integer conversion with proof before narrowing**

Signed token to int/long: compare against `INT_MIN/INT_MAX` or `LONG_MIN/LONG_MAX` before cast. UINT to signed: compare against `(uint64_t)INT_MAX` / `(uint64_t)LONG_MAX`. SINT to size_t: reject negative first; compare magnitude to `SIZE_MAX` before cast.

Representative int branch:

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

- [ ] **Step 6: Run scalar GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
ctest --preset test-release-linux -R '^(cmeta_data_test|cserde_)' --output-on-failure
```

Expected: PASS.

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

```c
bool cbind_u64_exact_in_binary(uint64_t magnitude, unsigned precision_bits);
bool cbind_s64_exact_in_binary(int64_t value, unsigned precision_bits);
bool cbind_double_is_integral(double value);
bool cbind_double_fits_signed_bits(double value, unsigned bits);
bool cbind_double_fits_unsigned_bits(double value, unsigned bits);
```

All remain private.

- [ ] **Step 1: Write numeric-boundary RED cases**

Required cases:

```text
FLOAT 42.0 -> int/long                       OK
FLOAT 42.5 -> int                            VALUE_OUT_OF_RANGE
FLOAT NaN/+Inf/-Inf -> integer               VALUE_OUT_OF_RANGE
FLOAT signed lower bound                     OK
FLOAT signed upper-exclusive bound           VALUE_OUT_OF_RANGE
FLOAT -1.0 -> size_t                         VALUE_OUT_OF_RANGE
FLOAT 0.0 -> size_t                          OK
FLOAT -> double NaN/Inf                      OK and preserved
FLOAT DBL_MAX -> float                       VALUE_OUT_OF_RANGE
FLOAT DBL_TRUE_MIN -> float                  VALUE_OUT_OF_RANGE when result is zero
FLOAT 1.0 + DBL_EPSILON -> float             OK; normal rounding allowed
SINT/UINT 2^24 -> float                      OK
SINT/UINT 2^24+1 -> float                    VALUE_OUT_OF_RANGE
SINT/UINT 2^53 -> double                     OK
SINT/UINT 2^53+1 -> double                   VALUE_OUT_OF_RANGE
INT64_MIN exactness path                     no signed-overflow in magnitude logic
UINT64_MAX -> double                         VALUE_OUT_OF_RANGE
```

Use `<float.h>`, `<math.h>`, `<limits.h>`, `<stdint.h>` and `ldexp()` for exact powers of two.

- [ ] **Step 2: Run numeric RED and commit**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
```

Expected: new FLOAT/exactness cases FAIL while Task 2 cases remain PASS.

```bash
git add cbind/tests/cbind_scalar_decode_test.c
git commit -m "test(cbind): specify strict numeric conversion"
```

- [ ] **Step 3: Implement safe double-to-integer proof**

Integral proof:

```c
bool cbind_double_is_integral(double value) {
    double integral;
    return isfinite(value) && modf(value, &integral) == 0.0;
}
```

For supported Linux/MSVC native integer storage, prove signed N-bit half-open range before cast:

```text
-2^(N-1) <= value < 2^(N-1)
```

and unsigned:

```text
0 <= value < 2^N
```

Compute powers with `ldexp(1.0, exponent)`. This avoids accepting binary64 `2^63` because `(double)INT64_MAX` rounded upward. Only cast after finite/integral/range proof.

- [ ] **Step 4: Implement exact integer-to-binary-float proof without unsafe round-trip casts**

Safe signed magnitude:

```c
uint64_t magnitude;
if (value < 0)
    magnitude = (uint64_t)(-(value + 1)) + UINT64_C(1);
else
    magnitude = (uint64_t)value;
```

Exactness:

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

Call with `FLT_MANT_DIG` / `DBL_MANT_DIG`. For 64-bit input the maximum discarded count is 40 (binary32) or 11 (binary64), so shift width is safe.

- [ ] **Step 5: Implement binary64-to-binary32 policy**

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

Finite in-range rounding is accepted.

- [ ] **Step 6: Run numeric GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_scalar_decode_test$' --output-on-failure
```

Expected: PASS with exact `2^24/2^24+1` and `2^53/2^53+1` distinctions.

- [ ] **Step 7: Commit Task 3 GREEN**

```bash
git add cbind
git commit -m "feat(cbind): enforce exact numeric conversion"
```

---

### Task 4: Recursive semantic/storage preflight, cycle safety, depth, and exact scratch budget

**Files:**
- Create: `cbind/src/struct.c`
- Create: `cbind/tests/cbind_struct_decode_test.c`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/CMakeLists.txt`
- Modify: `cbind/tests/CMakeLists.txt`

**Interfaces:**

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

Task 4 completes STRUCT preflight/resource proof; runtime struct MAP decode is Task 5.

- [ ] **Step 1: Register struct test and define explicit semantic models**

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

Define reflected models:

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

Define explicit `cmeta_type_identity`, `cmeta_type_desc`, semantic field arrays, struct shapes, and data descriptors for both custom structs using the same pattern as `cmeta/tests/cmeta_data_test.c`. Outer semantic fields are `id`, `score`, `inner`; reflected `untouched` is deliberately nonsemantic.

- [ ] **Step 2: Write preflight RED cases with `source.index == 0u` on rejection**

Required cases:

```text
root empty struct max_depth=0                                  LIMIT_EXCEEDED
root empty struct max_depth=1, scratch=0                       preflight accepted
one-field root scratch=0                                       LIMIT_EXCEEDED
one-field root scratch=1                                       preflight accepted
8-field root scratch=1                                         preflight accepted
9-field root scratch=1                                         LIMIT_EXCEEDED
9-field root scratch=2                                         preflight accepted
nested root(1 byte)+inner(1 byte), scratch=1                   LIMIT_EXCEEDED
same nested path scratch=2                                     preflight accepted
sibling nested structs                                         budget uses max active path, not schema total
struct storage size mismatch                                   INVALID_SHAPE
struct storage alignment mismatch                              INVALID_SHAPE
semantic/reflected offset mismatch                             INVALID_SHAPE
semantic child storage size mismatch                           INVALID_SHAPE
semantic child storage alignment mismatch                      INVALID_SHAPE
duplicate semantic field name/mapping                          INVALID_SHAPE
field range outside parent storage                             INVALID_SHAPE
self-referential semantic descriptor cycle                     INVALID_SHAPE
nested valid-but-unsupported child                             UNSUPPORTED
```

Cycle fixture:

```c
cmeta_data_desc cycle_desc = valid_record_desc;
cmeta_data_field_desc cycle_field = valid_record_fields[0];
cmeta_data_struct_shape cycle_shape = valid_record_shape;

cycle_field.value = &cycle_desc;
cycle_shape.fields = &cycle_field;
cycle_shape.field_count = 1u;
cycle_desc.shape = &cycle_shape;
```

- [ ] **Step 3: Run preflight RED and commit**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_struct_decode_test$' --output-on-failure
```

Expected: struct resource/storage cases FAIL because STRUCT preflight is absent; scalar suite remains PASS.

```bash
git add cbind
git commit -m "test(cbind): specify struct preflight budgets"
```

- [ ] **Step 4: Implement overflow-safe bitmap and DFS cycle/resource proof**

```c
size_t cbind_bitmap_bytes(size_t field_count) {
    return field_count / 8u + (field_count % 8u != 0u ? 1u : 0u);
}
```

Before STRUCT recursion, scan active `cbind_validation_frame` ancestors by semantic descriptor address; repeat means `CBIND_INVALID_SHAPE`.

Resource proof:

```text
next_depth = depth + 1
next_depth > context->max_depth                         => LIMIT_EXCEEDED
frame_bytes = cbind_bitmap_bytes(field_count)
active_scratch > SIZE_MAX - frame_bytes                 => LIMIT_EXCEEDED
next_active = active_scratch + frame_bytes
max_scratch = max(max_scratch, next_active)
```

Each child recurses with the same `next_active`; siblings do not accumulate after each other.

- [ ] **Step 5: Implement complete struct storage proof**

Prove:

```text
shape/layout/storage_type non-NULL
storage_type->size == layout->size
storage_type->align == layout->align
semantic field names unique
resolved reflected field exists
semantic offset == reflected offset
reflected size == child storage size
reflected align == child storage align
field->offset <= parent size
child_size <= parent size - field->offset
no two semantic entries resolve to the same reflected field
child graph recursively valid/supported
```

Use subtraction form for range proof; do not evaluate unchecked `offset + child_size`.

- [ ] **Step 6: Run preflight GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test cbind_scalar_decode_test
ctest --preset test-release-linux -R '^cbind_(scalar|struct)_decode_test$' --output-on-failure
```

Expected: rejection/budget tests PASS. Cases labeled “preflight accepted” assert reader/provider was reached rather than expecting final struct success; Task 5 supplies runtime success semantics.

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

```c
cbind_status cbind_decode_value(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    void *out);
```

`cbind_decode_value()` routes scalar kinds to `cbind_decode_scalar()` and STRUCT to `cbind_decode_struct()`. Unsupported kinds cannot reach runtime after successful preflight.

- [ ] **Step 1: Write strict struct runtime RED cases**

Required token sequences:

```text
empty semantic struct: MAP_BEGIN MAP_END                 OK
flat struct descriptor order                            OK
flat struct reversed order                              OK
nested struct                                           OK
exact case-sensitive field key                          OK only exact case
unknown key                                             UNKNOWN_FIELD
known key repeated                                      DUPLICATE_FIELD
MAP_END before all required fields                      MISSING_FIELD
UINT/non-STRING map key                                 TOKEN_MISMATCH
ARRAY_BEGIN root                                        TOKEN_MISMATCH
known field with wrong scalar token                     TOKEN_MISMATCH
```

Transient non-NUL key:

```c
static const unsigned char id_key_bytes[] = {'i', 'd'};
cserde_token key = {
    .kind = CSERDE_STRING,
    .value.slice = {id_key_bytes, sizeof(id_key_bytes), CSERDE_VIEW_TRANSIENT}
};
```

Add sibling nested-struct success where schema-total bitmap bytes exceed provided scratch but max active path fits.

- [ ] **Step 2: Run struct RED and commit**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_struct_decode_test$' --output-on-failure
```

Expected: new runtime cases FAIL; Task 4 preflight cases remain PASS.

```bash
git add cbind/tests/cbind_struct_decode_test.c
git commit -m "test(cbind): specify strict struct map decode"
```

- [ ] **Step 3: Implement exact length-delimited field lookup**

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

Never allocate/copy/store the transient key slice.

- [ ] **Step 4: Implement scratch-frame and seen bitmap ownership**

At struct entry after MAP_BEGIN:

```text
frame_bytes = cbind_bitmap_bytes(field_count)
mark = state->scratch_used
defensive check frame_bytes <= context->scratch_size - mark
bitmap = state->scratch + mark when frame_bytes != 0
zero bitmap only when frame_bytes != 0
state->scratch_used = mark + frame_bytes
```

Seen helpers:

```c
static bool cbind_seen(const unsigned char *bitmap, size_t index) {
    return (bitmap[index / 8u] & (unsigned char)(1u << (index % 8u))) != 0u;
}

static void cbind_mark_seen(unsigned char *bitmap, size_t index) {
    bitmap[index / 8u] |= (unsigned char)(1u << (index % 8u));
}
```

Every struct exit restores `state->scratch_used = mark`. Zero-field struct with zero scratch never dereferences NULL scratch.

- [ ] **Step 5: Implement normative struct state machine**

```text
require MAP_BEGIN
loop read required token
  MAP_END:
    first unseen semantic field in descriptor order -> MISSING_FIELD
    none unseen -> success
  STRING:
    exact slice lookup
    unknown -> UNKNOWN_FIELD; unknown value remains unconsumed
    seen -> DUPLICATE_FIELD; duplicate value remains unconsumed
    child_out = (unsigned char *)out + field->offset
    recursively decode child
    mark seen only after child success
  other:
    TOKEN_MISMATCH; paired value remains unconsumed
```

Entering STRUCT increments semantic depth before reading its content. Root struct errors are depth 1; nested struct errors are depth 2+; scalar fields inherit current struct depth.

- [ ] **Step 6: Run struct GREEN and assert reader positions**

```bash
cmake --build --preset build-default-linux --target cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_struct_decode_test$' --output-on-failure
```

Assert exact recording indices so unknown/duplicate/non-string key failures stop before their paired value.

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

**Interfaces:** Finalizes recursive `cbind_value_reset()` and deterministic diagnostic writes; no new public API.

- [ ] **Step 1: Add rollback/source RED tests**

Use a small test-only status-injection provider, not a second recording source:

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

Required cases:

```text
empty scalar input                               UNEXPECTED_END / source DONE / depth 0
empty struct input                               UNEXPECTED_END / source DONE / depth 1
mid-struct DONE                                  UNEXPECTED_END / source DONE
provider SOURCE_ERROR                            SOURCE_ERROR / exact source status
provider VALUE_OUT_OF_RANGE                      SOURCE_ERROR / exact source status
sticky CALLBACK_ERROR from CSerde                SOURCE_ERROR / exact CALLBACK_ERROR
failure after first field                        all root semantic fields reset
nested child failure                             complete root semantic graph reset
wrong later field token                          earlier semantic writes reset
nonsemantic reflected field on success           unchanged
nonsemantic reflected field on rollback          unchanged
provider calls after failure                     exactly zero additional calls
```

Diagnostic cases:

```text
root scalar mismatch        shape=root scalar, field=NULL, depth=0
known root field mismatch   shape=child scalar, field=&root_field, depth=1
unknown root field          shape=root struct, field=NULL, depth=1
duplicate root field        shape=root struct, field=&duplicate_field, depth=1
missing nested field        shape=nested struct, field=&first_missing_nested_field, depth=2
```

- [ ] **Step 2: Run RED and commit**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_(scalar|struct)_decode_test$' --output-on-failure
```

Expected: rollback/error-attribution/source-prefix cases FAIL; prior success cases remain PASS.

```bash
git add cbind/tests
git commit -m "test(cbind): specify transactional failure semantics"
```

- [ ] **Step 3: Finalize recursive semantic reset**

```text
BOOL        typed bool false + memcpy
SINT        typed int/long zero + memcpy
UINT        typed size_t zero + memcpy
FLOAT       typed float/double +0 + memcpy
STRUCT      recurse semantic fields only by semantic offsets
```

Never raw-zero the whole struct. Root transaction:

```c
status = cbind_decode_value(&state, shape, NULL, 0u, out);
if (status != CBIND_OK)
    cbind_value_reset(shape, out);
else
    cbind_error_clear(error);
return status;
```

- [ ] **Step 4: Make diagnostic attribution monotonic toward the most-specific child**

A child failure already carrying deeper `shape/depth` is returned unchanged by outer frames. Local struct errors set current struct shape/depth. Known child decode receives canonical parent field pointer; unknown/non-string key uses field NULL.

- [ ] **Step 5: Run complete functional GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_scalar_decode_test cbind_struct_decode_test
ctest --preset test-release-linux -R '^cbind_(scalar|struct)_decode_test$' --output-on-failure
ctest --preset test-release-linux -R '^(cmeta_|cserde_|cbind_)' --output-on-failure
```

Expected: PASS.

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

**Interfaces:** Proves Tasks 1-6 public ABI/package/build constraints; no new binding semantics.

- [ ] **Step 1: Add C++17 linkage test without C++20 designated initialization**

```cpp
#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<cbind_context>::value, "context ABI");
static_assert(std::is_standard_layout<cbind_error>::value, "error ABI");
static_assert(CBIND_CONTEXT_ABI_VERSION == 1u, "context ABI version");
static_assert(CBIND_ERROR_ABI_VERSION == 1u, "error ABI version");

spec("CBind C++17 public linkage") {
  it("calls the C decoder from C++") {
    cserde_token tokens[1]{};
    cserde_recording_reader_context source{};
    cserde_reader reader{};
    cbind_context context = CBIND_CONTEXT_INIT(nullptr, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    tokens[0].kind = CSERDE_SINT;
    tokens[0].value.sint = 7;
    source.tokens = tokens;
    source.count = 1u;
    source.index = 0u;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, &error),
                CBIND_OK);
    check_equal(out, 7);
  }
}
```

Register with C++17/no extensions.

- [ ] **Step 2: Run C++ linkage GREEN**

```bash
cmake --build --preset build-default-linux --target cbind_header_cpp_test
ctest --preset test-release-linux -R '^cbind_header_cpp_test$' --output-on-failure
```

Expected: PASS. Public declarations use C linkage; no C++ wrapper layer is introduced.

- [ ] **Step 3: Run exact dependency/allocation/test-support audit**

```bash
rg -n "TurboUtils::(STL|Core|CFlow)|TurboParser|turbostl|cflow/|turbo_parser|parser/" cbind/include cbind/src cbind/CMakeLists.txt
rg -n "\b(malloc|calloc|realloc|free)\s*\(" cbind/src cbind/include
rg -n "^(const )?cserde_reader_ops cserde_recording_reader_ops|typedef struct cserde_recording_reader_context" cbind cserde/tests/support
```

Expected:

```text
first command: no forbidden production dependency/include match
second command: no match
third command: definitions/type declaration occur only under cserde/tests/support; CBind has no duplicate definition
```

Also inspect `cbind/CMakeLists.txt`: production `target_link_libraries(turbo_cbind ...)` is exactly PUBLIC CMeta + CSerde. Any mismatch means the responsible earlier task is incomplete; correct that task before continuing.

- [ ] **Step 4: Verify installed export from a clean Linux release tree**

```bash
rm -rf build/linux-gcc-release /tmp/turboutils-cbind-install /tmp/cbind-consumer
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
cmake --install build/linux-gcc-release --prefix /tmp/turboutils-cbind-install
```

Create external consumer:

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

Expected: configure/build/run PASS with only `TurboUtils::CBind` named by the consumer.

- [ ] **Step 5: Commit C++ conformance test**

```bash
git add cbind/tests
git commit -m "test(cbind): cover C++17 public linkage"
```

- [ ] **Step 6: Run fresh exact-head Linux acceptance**

```bash
rm -rf build/linux-gcc-release
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux --output-on-failure
ctest --preset test-release-linux -R '^(cmeta_|cserde_|cbind_|cflow_|turbostl_)' --output-on-failure
```

Expected: full and selected CTest PASS. Record exact head SHA and test counts.

- [ ] **Step 7: Push that exact head and require Windows conformance on the same SHA**

Existing workflow must execute through `VsDevCmd` and repository presets:

```text
cmake --preset release-win-msvc-ninja
cmake --build --preset build-release-windows
ctest --preset test-release-windows -R "^(cmeta_|cserde_|cbind_|cflow_|turbostl_)" --output-on-failure
```

Expected on the same SHA:

```text
Linux   fresh configure + build + full/selected tests PASS
Windows fresh configure + build + selected tests PASS
```

Any Windows `long`/size boundary defect is corrected using `<limits.h>` and actual `sizeof` facts; the contract is not weakened and LP64 is not assumed. After any correction commit, repeat Steps 6-7 on the new exact head.

---

## Final Review Checklist

```text
[ ] TurboUtils::CBind exports/installs correctly.
[ ] Production CBind links only CMeta + CSerde.
[ ] Public D2 surface contains no parser/CFlow/incremental API.
[ ] Unsupported semantic kinds fail before provider consumption.
[ ] Malformed semantic/storage graphs fail before provider consumption.
[ ] Scratch/depth failures fail before provider consumption.
[ ] Non-empty semantic destination fails before provider consumption.
[ ] BOOL/int/long/size_t/float/double storage is canonically proven.
[ ] Numeric boundaries and exact integer-to-float rules pass.
[ ] Struct MAP keys are exact length-delimited STRING slices.
[ ] Unknown/duplicate/missing/non-string keys follow strict D2 semantics.
[ ] Scratch uses active-path maximum with sibling reuse and no field-count cap.
[ ] Post-consumption failures reset the complete semantic root only.
[ ] Padding and nonsemantic reflected fields remain untouched.
[ ] Reader is not rewound/skipped after failure.
[ ] DONE vs source failure mapping preserves exact CSerde status.
[ ] Transient key pointers never escape dispatch.
[ ] Production heap allocation is absent.
[ ] CSerde recording support is reused, not duplicated.
[ ] C11 and C++17 consumers compile/link.
[ ] Fresh installed consumer links `TurboUtils::CBind`.
[ ] Exact same final head passes Linux and Windows conformance.
```

Only after this checklist and exact-head CI pass should the branch proceed to code review/integration choice; merge remains an explicit user decision.
