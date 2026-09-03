# CSerde Canonical Token Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the standalone `Salts::CSerde` C11 module that defines canonical tokens, versioned pull-reader/push-writer facades, bounded nested-value skipping, and deterministic recording test providers for the later CBind layer.

**Architecture:** CSerde is a format-neutral transport protocol with no dependency on CMeta, CFlow, Container, TurboParser, or Core. Providers implement append-safe versioned ops tables; public facades validate ABI prefixes and callback results, maintain sticky terminal/error state, and never own token payload memory. Reader structural walking exists only in `cserde_reader_skip_value`; writer v1 deliberately does not duplicate a complete nesting validator.

**Tech Stack:** ISO C11, C++17 public-header/linkage compatibility, CMake presets, CTest, TinyTest, GitHub Actions Linux/MSVC release gates.

**Spec:** `docs/superpowers/specs/2026-08-23-cserde-canonical-protocol-design.md`

## Global Constraints

- Implementation starts from the final `design/cserde-canonical-protocol` head containing the approved spec and this plan, on execution branch `feat/cserde-canonical-protocol`.
- Public target name is `Salts::CSerde`; concrete target is `salts_cserde` with export name `CSerde`.
- CSerde must not include or link CMeta, CFlow, Container, TurboParser, or `utils` Core.
- Public callable declarations in `token.h`, `reader.h`, and `writer.h` must use the standard `#ifdef __cplusplus extern "C" { ... }` guard so the C implementation links correctly from C++17.
- Canonical token kinds are exactly `NULL/BOOL/SINT/UINT/FLOAT/STRING/BYTES/ARRAY_BEGIN/ARRAY_END/MAP_BEGIN/MAP_END` in D1.
- `SINT` stores `int64_t`; `UINT` stores `uint64_t`; `FLOAT` stores `double`. Integer producers must never round-trip through `double`.
- STRING/BYTES payloads are borrowed `cserde_slice` views. CSerde never allocates, copies, frees, or assumes NUL termination for public token payloads.
- View lifetime is exactly `CSERDE_VIEW_TRANSIENT` or `CSERDE_VIEW_STABLE`; STABLE is not ownership transfer.
- Reader/writer ops ABI version is 1 and validation uses field-end required-prefix size, never `sizeof(current_struct)` as the minimum compatible producer size.
- Reader callback accepted statuses are exactly `OK`, `DONE`, `VALUE_OUT_OF_RANGE`, `LIMIT_EXCEEDED`, `UNSUPPORTED`, `SOURCE_ERROR`; any other callback status is normalized to `CALLBACK_ERROR`.
- Writer callback accepted statuses are exactly `OK`, `VALUE_OUT_OF_RANGE`, `LIMIT_EXCEEDED`, `UNSUPPORTED`, `SINK_ERROR`; any other callback status is normalized to `CALLBACK_ERROR`.
- Reader/writer provider context is borrowed and may be NULL if the provider callback supports it.
- Reader failed/done states are sticky. Writer failed/finished states are terminal. Caller precondition errors must not invoke providers or poison an otherwise usable READY facade.
- `cserde_reader_skip_value` consumes exactly one canonical value, permits arbitrary canonical MAP keys, detects malformed nesting, and performs no heap allocation.
- D1 does not add parser code, binding policy, CBind, container construction, schema policy, allocator registry, DOM, or CFlow integration.
- Recording support exists only below `BUILD_TESTS`, is not installed/exported, is reusable by later in-tree `cbind/tests`, and performs no heap allocation.
- Exact final head must pass fresh Linux and Windows release configure/build/test with `cserde_*` included in the existing conformance workflow.

---

## File Structure

Production files after D1:

```text
cserde/
  CMakeLists.txt
  include/cserde/
    cserde.h
    status.h
    token.h
    reader.h
    writer.h
  src/
    token.c
    reader.c
    writer.c
```

Test files after D1:

```text
cserde/tests/
  CMakeLists.txt
  cserde_token_test.c
  cserde_reader_test.c
  cserde_writer_test.c
  cserde_recording_test.c
  cserde_header_cpp_test.cpp
  support/
    recording.h
    recording.c
```

Repository files modified:

```text
CMakeLists.txt
.github/workflows/cmeta.yml
```

---

### Task 1: Module skeleton, status model, canonical token validation, and CI trigger

**Files:**
- Create: `cserde/CMakeLists.txt`
- Create: `cserde/include/cserde/status.h`
- Create: `cserde/include/cserde/token.h`
- Create: `cserde/include/cserde/cserde.h`
- Create: `cserde/src/token.c`
- Create: `cserde/tests/CMakeLists.txt`
- Create: `cserde/tests/cserde_token_test.c`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: only C runtime headers (`stdbool.h`, `stddef.h`, `stdint.h`).
- Produces:

```c
typedef enum cserde_status {
    CSERDE_OK = 0,
    CSERDE_DONE,
    CSERDE_INVALID_ARGUMENT,
    CSERDE_INVALID_STATE,
    CSERDE_INVALID_TOKEN,
    CSERDE_UNEXPECTED_END,
    CSERDE_VALUE_OUT_OF_RANGE,
    CSERDE_LIMIT_EXCEEDED,
    CSERDE_UNSUPPORTED,
    CSERDE_SOURCE_ERROR,
    CSERDE_SINK_ERROR,
    CSERDE_CALLBACK_ERROR
} cserde_status;

typedef enum cserde_view_lifetime {
    CSERDE_VIEW_TRANSIENT = 0,
    CSERDE_VIEW_STABLE
} cserde_view_lifetime;

typedef struct cserde_slice {
    const unsigned char *data;
    size_t size;
    cserde_view_lifetime lifetime;
} cserde_slice;

typedef enum cserde_token_kind {
    CSERDE_NULL = 0,
    CSERDE_BOOL,
    CSERDE_SINT,
    CSERDE_UINT,
    CSERDE_FLOAT,
    CSERDE_STRING,
    CSERDE_BYTES,
    CSERDE_ARRAY_BEGIN,
    CSERDE_ARRAY_END,
    CSERDE_MAP_BEGIN,
    CSERDE_MAP_END
} cserde_token_kind;

typedef struct cserde_token {
    cserde_token_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double floating;
        cserde_slice slice;
    } value;
} cserde_token;

bool cserde_token_kind_valid(cserde_token_kind kind);
bool cserde_view_lifetime_valid(cserde_view_lifetime lifetime);
bool cserde_token_valid(const cserde_token *token);
```

- [ ] **Step 1: Add public declarations and build/test scaffold, leaving validation symbols undefined**

`cserde/CMakeLists.txt`:

```cmake
set(TARGET_NAME salts_cserde)

add_library(${TARGET_NAME} src/token.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::CSerde
  FOLDER "cserde"
  EXPORT_NAME CSerde)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cserde
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

Root `CMakeLists.txt`:

```cmake
add_subdirectory(cmeta)
add_subdirectory(cserde)
add_subdirectory(cflow)
```

Initial `cserde/tests/CMakeLists.txt`:

```cmake
cmake_add_test(cserde_token_test
  SOURCES cserde_token_test.c
  LIBS Salts::CSerde Salts::TinyTest
  FOLDER "cserde/tests")

set_target_properties(cserde_token_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

`token.h` must wrap the three function declarations in C linkage guards:

```c
#ifdef __cplusplus
extern "C" {
#endif

bool cserde_token_kind_valid(cserde_token_kind kind);
bool cserde_view_lifetime_valid(cserde_view_lifetime lifetime);
bool cserde_token_valid(const cserde_token *token);

#ifdef __cplusplus
}
#endif
```

`src/token.c` exists so configure succeeds but contains no function definitions for the initial RED.

Modify `.github/workflows/cmeta.yml` in both pull-request and push path filters:

```yaml
      - "cserde/**"
```

Change both selected-test regexes to:

```text
^(cmeta_|cserde_|cflow_|cstl_)
```

and rename the Linux test step to `Test CMeta, CSerde, CFlow, and Container`.

- [ ] **Step 2: Write token RED tests**

`cserde/tests/cserde_token_test.c` must include the following behaviors:

```c
#include <cserde/token.h>
#include "tinytest.h"

spec("CSerde canonical tokens") {
    it("accepts every canonical token kind") {
        int kind;
        for (kind = CSERDE_NULL; kind <= CSERDE_MAP_END; ++kind)
            check_true(cserde_token_kind_valid((cserde_token_kind)kind));
        check_false(cserde_token_kind_valid((cserde_token_kind)-1));
        check_false(cserde_token_kind_valid(
            (cserde_token_kind)(CSERDE_MAP_END + 1)));
    }

    it("accepts zero length borrowed slices with null data") {
        cserde_token string_token = {
            .kind = CSERDE_STRING,
            .value.slice = { NULL, 0u, CSERDE_VIEW_TRANSIENT }
        };
        cserde_token bytes_token = {
            .kind = CSERDE_BYTES,
            .value.slice = { NULL, 0u, CSERDE_VIEW_STABLE }
        };
        check_true(cserde_token_valid(&string_token));
        check_true(cserde_token_valid(&bytes_token));
    }

    it("rejects nonempty slices without backing data") {
        cserde_token token = {
            .kind = CSERDE_STRING,
            .value.slice = { NULL, 1u, CSERDE_VIEW_STABLE }
        };
        check_false(cserde_token_valid(&token));
    }

    it("rejects invalid slice lifetimes") {
        static const unsigned char text[] = "x";
        cserde_token token = {
            .kind = CSERDE_BYTES,
            .value.slice = { text, 1u, (cserde_view_lifetime)99 }
        };
        check_false(cserde_token_valid(&token));
    }
}
```

Also assert `cserde_token_valid(NULL) == false`, both legal lifetime values are accepted, invalid lifetime values are rejected, and every non-slice token kind is shallow-valid regardless of inactive union bytes.

- [ ] **Step 3: Run first RED and commit it**

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target cserde_token_test
```

Expected: configure succeeds; link fails only on the three validation functions.

```bash
git add CMakeLists.txt .github/workflows/cmeta.yml cserde
git commit -m "test(cserde): define canonical token contract"
```

Immediately open a **draft PR** from `feat/cserde-canonical-protocol` to `master` titled:

```text
feat(cserde): add canonical token protocol
```

The first PR workflow is expected RED at this exact test-only/API-contract head; record that run in the PR body as TDD evidence. Keep the PR draft through Tasks 1–6.

- [ ] **Step 4: Implement only shallow token validation**

`cserde/src/token.c`:

```c
#include <cserde/token.h>

bool cserde_token_kind_valid(cserde_token_kind kind) {
    return kind >= CSERDE_NULL && kind <= CSERDE_MAP_END;
}

bool cserde_view_lifetime_valid(cserde_view_lifetime lifetime) {
    return lifetime == CSERDE_VIEW_TRANSIENT ||
           lifetime == CSERDE_VIEW_STABLE;
}

bool cserde_token_valid(const cserde_token *token) {
    if (token == NULL || !cserde_token_kind_valid(token->kind))
        return false;
    if (token->kind != CSERDE_STRING && token->kind != CSERDE_BYTES)
        return true;
    if (!cserde_view_lifetime_valid(token->value.slice.lifetime))
        return false;
    return token->value.slice.size == 0u || token->value.slice.data != NULL;
}
```

`cserde.h` includes `status.h` and `token.h` at this task.

- [ ] **Step 5: Run GREEN and dependency audit**

```bash
cmake --build --preset build-default-linux --target cserde_token_test salts_cserde
ctest --preset test-release-linux -R '^cserde_token_test$' --output-on-failure
```

Expected: PASS. `cserde/CMakeLists.txt` contains no production `target_link_libraries` dependency on CMeta/CFlow/Container/Core.

- [ ] **Step 6: Commit Task 1 GREEN**

```bash
git add cserde
git commit -m "feat(cserde): add canonical token model"
```

---

### Task 2: Versioned pull-reader facade and sticky state semantics

**Files:**
- Create: `cserde/include/cserde/reader.h`
- Create: `cserde/src/reader.c`
- Create: `cserde/tests/cserde_reader_test.c`
- Modify: `cserde/include/cserde/cserde.h`
- Modify: `cserde/CMakeLists.txt`
- Modify: `cserde/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 status/token API.
- Produces:

```c
enum { CSERDE_READER_OPS_ABI_VERSION = 1u };

typedef cserde_status (*cserde_reader_next_fn)(
    void *context,
    cserde_token *out);

typedef struct cserde_reader_ops {
    size_t struct_size;
    uint32_t abi_version;
    cserde_reader_next_fn next;
} cserde_reader_ops;

typedef enum cserde_reader_state {
    CSERDE_READER_ZERO = 0,
    CSERDE_READER_READY,
    CSERDE_READER_DONE,
    CSERDE_READER_FAILED
} cserde_reader_state;

typedef struct cserde_reader {
    const cserde_reader_ops *ops;
    void *context;
    cserde_reader_state state;
    cserde_status status;
} cserde_reader;

cserde_status cserde_reader_init(
    cserde_reader *reader,
    const cserde_reader_ops *ops,
    void *context);

cserde_status cserde_reader_next(
    cserde_reader *reader,
    cserde_token *out);

cserde_status cserde_reader_skip_value(
    cserde_reader *reader,
    size_t max_depth);
```

`reader.h` wraps all callable declarations in `extern "C"` guards. `skip_value` is declared now and implemented in Task 3.

- [ ] **Step 1: Add reader declarations and RED tests without reader function bodies**

Fake provider:

```c
typedef struct fake_reader_context {
    cserde_status next_status;
    cserde_token token;
    size_t calls;
} fake_reader_context;

static cserde_status fake_reader_next(void *context, cserde_token *out) {
    fake_reader_context *state = (fake_reader_context *)context;
    ++state->calls;
    if (state->next_status == CSERDE_OK)
        *out = state->token;
    return state->next_status;
}
```

Required test cases:

1. exact v1 prefix through `.next` initializes successfully;
2. prefix one byte short is rejected and zero reader remains byte-for-byte zero;
3. wrong ABI is rejected without mutation;
4. NULL `.next` is rejected without mutation;
5. NULL context is accepted when provider supports it;
6. OK callback commits a valid temporary token to caller output;
7. OK callback with invalid token -> `INVALID_TOKEN`, FAILED/sticky, caller output unchanged;
8. DONE -> DONE/sticky and repeated next does not call provider;
9. each allowed reader callback error -> FAILED/sticky;
10. `CSERDE_SINK_ERROR` from reader callback -> `CALLBACK_ERROR`;
11. unknown enum status -> `CALLBACK_ERROR`;
12. NULL reader/output caller preconditions do not advance or poison READY reader;
13. next on ZERO -> `INVALID_STATE` without callback.

Sentinel-output proof:

```c
cserde_token out = {
    .kind = CSERDE_UINT,
    .value.uint = UINT64_C(0x1234)
};
check_equal(cserde_reader_next(&reader, &out), CSERDE_SOURCE_ERROR);
check_true(out.kind == CSERDE_UINT);
check_equal(out.value.uint, UINT64_C(0x1234));
```

- [ ] **Step 2: Run and commit reader RED**

```bash
cmake --build --preset build-default-linux --target cserde_reader_test
```

Expected: link failure only for reader facade symbols.

```bash
git add cserde/include/cserde cserde/tests
git commit -m "test(cserde): define pull reader contract"
```

- [ ] **Step 3: Implement append-safe reader ABI and state facade**

Add `src/reader.c` to `salts_cserde` and use a field-end prefix check:

```c
#define CSERDE_FIELD_END(type, field) \
    (offsetof(type, field) + sizeof(((type *)0)->field))

static bool cserde_reader_ops_valid(const cserde_reader_ops *ops) {
    return ops != NULL &&
           ops->struct_size >= CSERDE_FIELD_END(cserde_reader_ops, next) &&
           ops->abi_version == CSERDE_READER_OPS_ABI_VERSION &&
           ops->next != NULL;
}
```

Initialization must validate zero state first and commit a temporary facade only after all validation succeeds:

```c
if (reader == NULL || ops == NULL)
    return CSERDE_INVALID_ARGUMENT;
if (reader->ops != NULL || reader->context != NULL ||
    reader->state != CSERDE_READER_ZERO || reader->status != CSERDE_OK)
    return CSERDE_INVALID_STATE;
if (!cserde_reader_ops_valid(ops))
    return CSERDE_INVALID_ARGUMENT;
```

`cserde_reader_next` invokes provider with a local `cserde_token temporary`; only callback `OK` plus `cserde_token_valid(&temporary)` copies to caller `out`. Allowed callback errors become FAILED/sticky. DONE becomes DONE/sticky. Every disallowed/unknown callback status becomes `CALLBACK_ERROR`/FAILED.

- [ ] **Step 4: Run reader GREEN plus token regression**

```bash
cmake --build --preset build-default-linux --target cserde_token_test cserde_reader_test
ctest --preset test-release-linux -R '^(cserde_token_test|cserde_reader_test)$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 2 GREEN**

```bash
git add cserde
git commit -m "feat(cserde): add versioned pull reader"
```

---

### Task 3: Bounded `skip_value` structural walker

**Files:**
- Modify: `cserde/src/reader.c`
- Modify: `cserde/tests/cserde_reader_test.c`

**Interfaces:**
- Consumes: `cserde_reader_next` and Task 1 token grammar.
- Produces: `cserde_reader_skip_value(cserde_reader *, size_t max_depth)` with exact one-value consumption and no allocation.

- [ ] **Step 1: Add RED cases using a fixed sequence provider**

```c
typedef struct sequence_reader_context {
    const cserde_token *tokens;
    size_t count;
    size_t index;
} sequence_reader_context;

static cserde_status sequence_reader_next(void *context, cserde_token *out) {
    sequence_reader_context *state = (sequence_reader_context *)context;
    if (state->index == state->count)
        return CSERDE_DONE;
    *out = state->tokens[state->index++];
    return CSERDE_OK;
}
```

Test these exact token sequences:

```text
SINT(7), UINT(9)
  skip once -> next is UINT(9)

ARRAY_BEGIN, SINT(1), ARRAY_BEGIN, BOOL(true), ARRAY_END, ARRAY_END, UINT(9)
  skip(max_depth=2) -> next is UINT(9)

MAP_BEGIN, ARRAY_BEGIN, SINT(1), ARRAY_END, STRING("value"), MAP_END, NULL
  skip(max_depth=2) -> next is NULL

ARRAY_BEGIN, SINT(1), MAP_END
  -> INVALID_TOKEN + FAILED

MAP_BEGIN, STRING("key"), MAP_END
  -> INVALID_TOKEN + FAILED

empty stream
  -> UNEXPECTED_END + FAILED

ARRAY_BEGIN, SINT(1), then DONE
  -> UNEXPECTED_END + FAILED

ARRAY_BEGIN, ARRAY_BEGIN, NULL, ARRAY_END, ARRAY_END
  max_depth=1 -> LIMIT_EXCEEDED + FAILED
  max_depth=2 -> OK

scalar with max_depth=0
  -> OK
ARRAY_BEGIN with max_depth=0
  -> LIMIT_EXCEEDED + FAILED
```

After every failed skip, verify another `cserde_reader_next` returns the same sticky error with no provider call.

- [ ] **Step 2: Run and commit skip RED**

```bash
cmake --build --preset build-default-linux --target cserde_reader_test
```

Expected: undefined `cserde_reader_skip_value` or failing skip assertions.

```bash
git add cserde/tests/cserde_reader_test.c
git commit -m "test(cserde): define nested skip semantics"
```

- [ ] **Step 3: Implement one-value grammar walking**

Use an already-read-token helper so container END markers are consumed by their owning loop:

```c
static cserde_status cserde_reader_skip_token(
    cserde_reader *reader,
    const cserde_token *token,
    size_t depth,
    size_t max_depth);

static cserde_status cserde_reader_take_token(
    cserde_reader *reader,
    cserde_token *token);
```

`take_token` maps DONE encountered while a complete value is required to:

```c
reader->state = CSERDE_READER_FAILED;
reader->status = CSERDE_UNEXPECTED_END;
return CSERDE_UNEXPECTED_END;
```

When handling an ARRAY/MAP begin token, reject if `depth >= max_depth`. Root begins are called with `depth=0`, so `max_depth=1` allows one container level and a nested begin fails. Scalars do not consume depth budget.

ARRAY loop:

```c
for (;;) {
    cserde_token child;
    cserde_status status = cserde_reader_take_token(reader, &child);
    if (status != CSERDE_OK)
        return status;
    if (child.kind == CSERDE_ARRAY_END)
        return CSERDE_OK;
    if (child.kind == CSERDE_MAP_END)
        return cserde_reader_fail(reader, CSERDE_INVALID_TOKEN);
    status = cserde_reader_skip_token(reader, &child, depth + 1u, max_depth);
    if (status != CSERDE_OK)
        return status;
}
```

MAP loop must read a possible key/`MAP_END`, recursively consume the key value, then require and recursively consume exactly one value. An END token in value position is `INVALID_TOKEN`. Canonical map keys may themselves be arrays/maps.

Top-level END is `INVALID_TOKEN`. Any failure from skip poisons reader because source position may already have advanced.

- [ ] **Step 4: Run reader GREEN and commit**

```bash
cmake --build --preset build-default-linux --target cserde_reader_test
ctest --preset test-release-linux -R '^cserde_reader_test$' --output-on-failure
```

```bash
git add cserde/src/reader.c cserde/tests/cserde_reader_test.c
git commit -m "feat(cserde): add bounded value skipping"
```

---

### Task 4: Versioned push-writer facade and byte sink contract

**Files:**
- Create: `cserde/include/cserde/writer.h`
- Create: `cserde/src/writer.c`
- Create: `cserde/tests/cserde_writer_test.c`
- Modify: `cserde/include/cserde/cserde.h`
- Modify: `cserde/CMakeLists.txt`
- Modify: `cserde/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 status/token API.
- Produces:

```c
enum { CSERDE_WRITER_OPS_ABI_VERSION = 1u };

typedef cserde_status (*cserde_writer_write_token_fn)(
    void *context,
    const cserde_token *token);

typedef cserde_status (*cserde_writer_finish_fn)(void *context);

typedef struct cserde_writer_ops {
    size_t struct_size;
    uint32_t abi_version;
    cserde_writer_write_token_fn write;
    cserde_writer_finish_fn finish;
} cserde_writer_ops;

typedef enum cserde_writer_state {
    CSERDE_WRITER_ZERO = 0,
    CSERDE_WRITER_READY,
    CSERDE_WRITER_FINISHED,
    CSERDE_WRITER_FAILED
} cserde_writer_state;

typedef struct cserde_writer {
    const cserde_writer_ops *ops;
    void *context;
    cserde_writer_state state;
    cserde_status status;
} cserde_writer;

typedef cserde_status (*cserde_byte_sink_fn)(
    void *context,
    const void *data,
    size_t size);

cserde_status cserde_writer_init(
    cserde_writer *writer,
    const cserde_writer_ops *ops,
    void *context);

cserde_status cserde_writer_write(
    cserde_writer *writer,
    const cserde_token *token);

cserde_status cserde_writer_finish(cserde_writer *writer);
```

`writer.h` wraps callable declarations and callback typedefs in C linkage guards. D1 writer has no abort and no canonical nesting stack.

- [ ] **Step 1: Add writer declarations and RED tests before `writer.c`**

Fake provider:

```c
typedef struct fake_writer_context {
    cserde_status write_status;
    cserde_status finish_status;
    size_t write_calls;
    size_t finish_calls;
    cserde_token observed;
} fake_writer_context;
```

Required tests:

1. exact prefix through `.finish` initializes;
2. one-byte-short prefix, bad ABI, NULL write, NULL finish reject without mutating zero writer;
3. valid token forwards exactly once;
4. invalid STRING/BYTES token returns `INVALID_TOKEN`, does not call provider, leaves writer READY;
5. NULL writer/token preconditions do not call provider or poison READY writer;
6. allowed write callback errors (`VALUE_OUT_OF_RANGE`, `LIMIT_EXCEEDED`, `UNSUPPORTED`, `SINK_ERROR`) become FAILED/sticky;
7. `SOURCE_ERROR`, `DONE`, and unknown callback status become `CALLBACK_ERROR`;
8. successful finish -> FINISHED and one callback;
9. repeated finish and write-after-finish -> `INVALID_STATE` without callback;
10. failed finish -> FAILED/sticky;
11. a function matching `(void *, const void *, size_t)` assigns to `cserde_byte_sink_fn` in C without cast.

- [ ] **Step 2: Run and commit writer RED**

```bash
cmake --build --preset build-default-linux --target cserde_writer_test
```

Expected: link failure only for writer facade symbols.

```bash
git add cserde/include/cserde cserde/tests
git commit -m "test(cserde): define push writer contract"
```

- [ ] **Step 3: Implement writer ABI/state facade**

Add `src/writer.c` to `salts_cserde`. Validate ops prefix through `.finish` using the same field-end rule. Init uses temporary-then-commit semantics.

Write flow:

```text
validate caller pointers/state
  -> validate token shallowly
  -> call provider write
  -> OK: remain READY
  -> allowed provider error: FAILED/sticky
  -> other callback result: CALLBACK_ERROR/FAILED
```

Invalid caller token is not a provider-stream failure and leaves writer READY. `finish` is legal only from READY; callback OK -> FINISHED, allowed writer callback error -> FAILED, other status -> CALLBACK_ERROR/FAILED.

Do not add container depth, map parity, abort, allocation, or serialization logic.

- [ ] **Step 4: Run writer GREEN and commit**

```bash
cmake --build --preset build-default-linux --target cserde_token_test cserde_reader_test cserde_writer_test
ctest --preset test-release-linux -R '^cserde_' --output-on-failure
```

```bash
git add cserde
git commit -m "feat(cserde): add versioned push writer"
```

---

### Task 5: Reusable test-only recording providers and C++17 public linkage

**Files:**
- Create: `cserde/tests/support/recording.h`
- Create: `cserde/tests/support/recording.c`
- Create: `cserde/tests/cserde_recording_test.c`
- Create: `cserde/tests/cserde_header_cpp_test.cpp`
- Modify: `cserde/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: public CSerde API.
- Produces test-only, in-tree reusable support:

```c
typedef struct cserde_recording_reader_context {
    const cserde_token *tokens;
    size_t count;
    size_t index;
} cserde_recording_reader_context;

typedef struct cserde_recording_writer_context {
    cserde_token *tokens;
    size_t capacity;
    size_t count;
    bool finished;
} cserde_recording_writer_context;

extern const cserde_reader_ops cserde_recording_reader_ops;
extern const cserde_writer_ops cserde_recording_writer_ops;
```

Recording reader returns array tokens then DONE. Recording writer shallow-copies a token while capacity remains, otherwise returns `LIMIT_EXCEEDED`. STRING/BYTES payload bytes remain caller-owned.

- [ ] **Step 1: Define a BUILD_TESTS-only reusable support target and write RED recording tests**

In `cserde/tests/CMakeLists.txt` add:

```cmake
add_library(cserde_recording_support STATIC support/recording.c)
target_link_libraries(cserde_recording_support PUBLIC Salts::CSerde)
target_include_directories(
  cserde_recording_support
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
cmake_config_target(cserde_recording_support FOLDER "cserde/tests")

cmake_add_test(cserde_recording_test
  SOURCES cserde_recording_test.c
  LIBS cserde_recording_support Salts::TinyTest
  FOLDER "cserde/tests")
```

Because `cserde/tests` itself is entered only under `if(BUILD_TESTS)`, this target cannot enter install/export production surface. Later `cbind/tests` in the same build may link `cserde_recording_support` directly.

For the RED commit, `recording.h` declares the contexts/ops but `recording.c` does not yet define them.

Concrete input:

```c
static const unsigned char name[] = "alice";
static const cserde_token input[] = {
    { .kind = CSERDE_MAP_BEGIN },
    { .kind = CSERDE_STRING,
      .value.slice = { name, 5u, CSERDE_VIEW_STABLE } },
    { .kind = CSERDE_UINT, .value.uint = UINT64_C(7) },
    { .kind = CSERDE_MAP_END }
};
```

Verify reader emits four tokens then DONE; capacity-4 writer records four shallow tokens and finishes; recorded STRING pointer equals `name`; capacity-3 writer fails fourth write with `LIMIT_EXCEEDED`, count remains 3, facade becomes FAILED.

- [ ] **Step 2: Run and commit recording RED**

```bash
cmake --build --preset build-default-linux --target cserde_recording_test
```

Expected: link failure on recording ops symbols.

```bash
git add cserde/tests
git commit -m "test(cserde): define recording provider harness"
```

- [ ] **Step 3: Implement fixed-array recording providers**

No `malloc`, `calloc`, `realloc`, `free`, `strdup`, Container, or Core calls.

Use exact v1 prefix sizes:

```c
const cserde_reader_ops cserde_recording_reader_ops = {
    .struct_size = offsetof(cserde_reader_ops, next) +
                   sizeof(((cserde_reader_ops *)0)->next),
    .abi_version = CSERDE_READER_OPS_ABI_VERSION,
    .next = cserde_recording_reader_next
};
```

Writer ops uses the field end through `.finish`. Direct support callbacks use only context validation, index/capacity checks, shallow assignment, and the callback statuses allowed by the public facade.

- [ ] **Step 4: Add C++17 compile-and-link conformance**

`cserde_header_cpp_test.cpp`:

```cpp
#include <cserde/cserde.h>
#include "tinytest.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout<cserde_slice>::value, "slice ABI");
static_assert(std::is_standard_layout<cserde_token>::value, "token ABI");
static_assert(std::is_standard_layout<cserde_reader_ops>::value, "reader ops ABI");
static_assert(std::is_standard_layout<cserde_reader>::value, "reader facade ABI");
static_assert(std::is_standard_layout<cserde_writer_ops>::value, "writer ops ABI");
static_assert(std::is_standard_layout<cserde_writer>::value, "writer facade ABI");
static_assert(CSERDE_READER_OPS_ABI_VERSION == 1u, "reader ABI version");
static_assert(CSERDE_WRITER_OPS_ABI_VERSION == 1u, "writer ABI version");
```

Runtime TinyTest must call `cserde_token_valid`, `cserde_reader_init`, `cserde_reader_next`, `cserde_writer_init`, `cserde_writer_write`, and `cserde_writer_finish` from C++17. This is a link test for the `extern "C"` contract, not just an include test.

Register:

```cmake
cmake_add_test(cserde_header_cpp_test
  SOURCES cserde_header_cpp_test.cpp
  LIBS Salts::CSerde Salts::TinyTest
  FOLDER "cserde/tests")

set_target_properties(cserde_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
```

- [ ] **Step 5: Run all CSerde tests GREEN**

```bash
cmake --build --preset build-default-linux --target cserde_recording_test cserde_header_cpp_test
ctest --preset test-release-linux -R '^cserde_' --output-on-failure
```

Expected: all CSerde tests PASS.

- [ ] **Step 6: Commit Task 5 GREEN**

```bash
git add cserde/tests
git commit -m "test(cserde): add recording and C++ conformance"
```

---

### Task 6: Review hardening, fresh exact-head CI, and PR readiness

**Files:**
- Modify only files for concrete review defects; no new feature scope.
- Verify: `cserde/**`, `CMakeLists.txt`, `.github/workflows/cmeta.yml`.

**Interfaces:**
- Consumes: complete D1 surface.
- Produces: review-clean exact head suitable for merge; still no CBind or format-adapter production code.

- [ ] **Step 1: Audit dependency and scope boundaries**

```bash
rg -n "cmeta|cflow|container|salts_parser|TurboParser|malloc|calloc|realloc|free" \
  cserde/include cserde/src cserde/CMakeLists.txt
```

Expected: no production dependency/allocation hits.

Search format names separately:

```bash
rg -n "json|yaml|xml|csv" cserde/include cserde/src
```

Expected: no parser/serializer implementation. A documentation comment may name a format only if it explains a format-neutral boundary; remove incidental references.

Workflow/module wiring:

```bash
rg -n "cserde" CMakeLists.txt .github/workflows/cmeta.yml \
  cserde/CMakeLists.txt cserde/tests/CMakeLists.txt
```

Expected: root subdirectory, workflow path filters/test regex, module target, tests, and recording support target all present.

- [ ] **Step 2: Run formatting and diff checks**

```bash
clang-format --dry-run --Werror \
  cserde/include/cserde/*.h \
  cserde/src/*.c \
  cserde/tests/*.c \
  cserde/tests/*.cpp \
  cserde/tests/support/*.c \
  cserde/tests/support/*.h

git diff --check master...HEAD
```

Expected: both commands succeed.

- [ ] **Step 3: Fresh Linux release verification**

The preset binary directory is `build/linux-gcc-release`.

```bash
rm -rf build/linux-gcc-release
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux -R '^(cmeta_|cserde_|cflow_|cstl_)' --output-on-failure
```

Expected: fresh configure PASS, full build PASS, every selected test PASS.

- [ ] **Step 4: Fresh Windows/MSVC release verification**

The preset binary directory is `build/Msvc-Release`. In a `VsDevCmd.bat` x64 environment:

```bat
if exist build\Msvc-Release rmdir /s /q build\Msvc-Release
cmake --preset release-win-msvc-ninja
cmake --build --preset build-release-windows
ctest --preset test-release-windows -R "^(cmeta_|cserde_|cflow_|cstl_)" --output-on-failure
```

Expected: fresh configure/build/test PASS.

- [ ] **Step 5: Review every D1 acceptance item**

Explicitly verify:

1. token/status model has no format-specific token;
2. SINT/UINT/FLOAT widths are exact;
3. STRING/BYTES slice/lifetime validation matches spec;
4. reader ops prefix/ABI validation is append-safe;
5. reader init failure is non-mutating;
6. reader non-OK never overwrites caller output;
7. DONE/FAILED reader states are sticky;
8. reader callback status normalization is exact;
9. skip_value consumes exactly one nested value;
10. malformed END/map parity/early DONE/depth failures poison correctly;
11. writer ops prefix/ABI validation is append-safe;
12. invalid caller token does not invoke provider or poison READY writer;
13. writer callback normalization and FINISHED state are exact;
14. writer has no abort and no duplicate nesting stack;
15. recording support is fixed-capacity, no-allocation, reusable, and test-only;
16. C++17 public API compiles **and links** to the C implementation;
17. production target has no forbidden module/parser dependency;
18. exact-head Linux + Windows release CI is green.

For every defect found, add one focused failing regression first, observe RED, apply the smallest fix, then rerun the affected `cserde_*` tests.

- [ ] **Step 6: Commit concrete review fixes only when needed**

Example for the known class of non-mutating-output regression:

```bash
git add cserde
git commit -m "fix(cserde): preserve reader output on callback failure"
```

If review finds no defect, create no review-fix commit.

- [ ] **Step 7: Verify the draft PR on the exact final head and mark ready**

Update the existing draft PR body with:

```text
D1 scope: canonical token + versioned reader/writer + skip_value + reusable recording harness only.
No CBind, parser, Container, CMeta semantic, or container-construction production changes.
```

Record the exact final head SHA and the GitHub Actions run ID that executed that SHA. Require both Linux release and Windows release jobs to be `completed/success`. Only then mark the PR Ready for Review.

---

## Expected Commit Shape

```text
test(cserde): define canonical token contract        RED
feat(cserde): add canonical token model             GREEN
test(cserde): define pull reader contract           RED
feat(cserde): add versioned pull reader             GREEN
test(cserde): define nested skip semantics          RED
feat(cserde): add bounded value skipping            GREEN
test(cserde): define push writer contract           RED
feat(cserde): add versioned push writer             GREEN
test(cserde): define recording provider harness     RED
test(cserde): add recording and C++ conformance     GREEN
```

A focused `fix(cserde): ...` commit is added only if review or a platform gate first demonstrates a concrete failing regression. RED/GREEN evidence remains visible until review; do not squash it away during implementation.
