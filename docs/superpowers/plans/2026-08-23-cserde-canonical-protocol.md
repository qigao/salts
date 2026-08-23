# CSerde Canonical Token Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the standalone `TurboUtils::CSerde` C11 module that defines canonical tokens, versioned pull-reader/push-writer facades, bounded nested-value skipping, and deterministic recording test providers for the later CBind layer.

**Architecture:** CSerde is a format-neutral transport protocol with no dependency on CMeta, CFlow, TurboSTL, TurboParser, or Core. Providers implement versioned ops tables; the public facades validate ABI prefixes and callback results, maintain sticky terminal/error state, and never own token payload memory. Reader structural walking lives only in `cserde_reader_skip_value`; writer v1 deliberately does not duplicate a complete output nesting validator.

**Tech Stack:** ISO C11, C++17 public-header compatibility, CMake presets, CTest, TinyTest, GitHub Actions Linux/MSVC release gates.

**Spec:** `docs/superpowers/specs/2026-08-23-cserde-canonical-protocol-design.md`

## Global Constraints

- Implementation baseline is `master` commit `4e2d47971071bb75266a56844919a79e724c5c82` plus the approved D1 spec/plan commits.
- Public target name is `TurboUtils::CSerde`; concrete target is `turbo_cserde` with export name `CSerde`.
- CSerde must not include or link CMeta, CFlow, TurboSTL, TurboParser, or `utils` Core.
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
- Test-only recording support is not installed/exported and performs no heap allocation.
- Exact final head must pass fresh Linux and Windows release configure/build/test with `cserde_*` included in the existing conformance workflow.

---

## File Structure

Production files after D1:

```text
cserde/
  CMakeLists.txt                    target/export/install/test wiring
  include/cserde/
    cserde.h                        umbrella public include only
    status.h                        cserde_status
    token.h                         canonical token/slice model + validation API
    reader.h                        pull reader provider ABI + facade API
    writer.h                        push writer provider ABI + facade API + byte sink typedef
  src/
    token.c                         shallow token/view validation
    reader.c                        reader ABI/state facade + skip_value walker
    writer.c                        writer ABI/state facade
```

Test files after D1:

```text
cserde/tests/
  CMakeLists.txt
  cserde_token_test.c               token/status/lifetime contract
  cserde_reader_test.c              reader ABI/state/callback/skip contract
  cserde_writer_test.c              writer ABI/state/callback contract
  cserde_recording_test.c           recording provider conformance
  cserde_header_cpp_test.cpp        C++17 standard-layout/public include contract
  support/
    recording.h                     test-only fixed-array provider API
    recording.c                     test-only reader/writer callbacks
```

Repository files modified:

```text
CMakeLists.txt                       add_subdirectory(cserde)
.github/workflows/cmeta.yml          trigger on cserde/** and execute cserde_* tests
```

---

### Task 1: Module skeleton, status model, and canonical token validation

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

- [ ] **Step 1: Add the test/build scaffold and public declarations, but no validation implementation**

Create `cserde/CMakeLists.txt` with the final library ownership but leave `src/token.c` declaration-only/empty enough that validation symbols remain undefined for the first RED:

```cmake
set(TARGET_NAME turbo_cserde)

add_library(${TARGET_NAME} src/token.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::CSerde
  FOLDER "cserde"
  EXPORT_NAME CSerde)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
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

Modify the root `CMakeLists.txt` immediately after `add_subdirectory(cmeta)`:

```cmake
add_subdirectory(cmeta)
add_subdirectory(cserde)
add_subdirectory(cflow)
```

Create `cserde/tests/CMakeLists.txt` initially with:

```cmake
cmake_add_test(cserde_token_test
  SOURCES cserde_token_test.c
  LIBS TurboUtils::CSerde TurboUtils::TinyTest
  FOLDER "cserde/tests")

set_target_properties(cserde_token_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

Modify `.github/workflows/cmeta.yml` in both pull-request and push path filters to add:

```yaml
      - "cserde/**"
```

and change both test regexes to:

```text
^(cmeta_|cserde_|cflow_|turbostl_)
```

so every later D1 commit is exercised by the existing Linux/Windows release gate.

- [ ] **Step 2: Write the token validation tests before function bodies**

`cserde/tests/cserde_token_test.c` must cover all token kinds and both slice lifetimes, including these concrete cases:

```c
#include <cserde/token.h>
#include "tinytest.h"

spec("CSerde canonical tokens") {
    it("accepts every canonical token kind") {
        int kind;
        for (kind = CSERDE_NULL; kind <= CSERDE_MAP_END; ++kind)
            check_true(cserde_token_kind_valid((cserde_token_kind)kind));
        check_false(cserde_token_kind_valid((cserde_token_kind)-1));
        check_false(cserde_token_kind_valid((cserde_token_kind)(CSERDE_MAP_END + 1)));
    }

    it("accepts zero-length borrowed slices with null data") {
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

    it("rejects non-empty slices without backing data") {
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

    it("does not interpret scalar payload contents") {
        cserde_token floating = { .kind = CSERDE_FLOAT };
        floating.value.floating = 0.0 / 0.0;
        check_true(cserde_token_valid(&floating));
    }
}
```

Also assert `cserde_token_valid(NULL) == false`, both legal lifetime values, and invalid lifetime values.

- [ ] **Step 3: Run the first RED**

Run on Linux/local compatible toolchain:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target cserde_token_test
```

Expected: configure succeeds; link fails on one or more of `cserde_token_kind_valid`, `cserde_view_lifetime_valid`, `cserde_token_valid`. The failure must not be a missing CMake target, include path, or unrelated repository error.

Commit this RED contract:

```bash
git add CMakeLists.txt .github/workflows/cmeta.yml cserde
 git commit -m "test(cserde): define canonical token contract"
```

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

`cserde/include/cserde/cserde.h` at this stage includes `status.h` and `token.h`; reader/writer includes are appended in their tasks.

- [ ] **Step 5: Run GREEN and dependency audit**

```bash
cmake --build --preset build-default-linux --target cserde_token_test
ctest --preset test-release-linux -R '^cserde_token_test$' --output-on-failure
```

Expected: PASS.

Check CSerde has no target dependency:

```bash
cmake --build --preset build-default-linux --target turbo_cserde
```

Then inspect `cserde/CMakeLists.txt`: it must contain no `target_link_libraries(turbo_cserde ... CMeta/CFlow/TurboSTL/Core ...)` entry.

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
- Consumes: `cserde_status`, `cserde_token`, `cserde_token_valid` from Task 1.
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

`cserde_reader_skip_value` is declared now but implemented only in Task 3.

- [ ] **Step 1: Add reader declarations and RED tests, without reader function bodies**

Add `reader.h`, include it from `cserde.h`, register `cserde_reader_test`, but do not yet add `src/reader.c` to `turbo_cserde`.

Use a fake provider whose context records call count and returns a programmed status/token:

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

Required test cases in `cserde_reader_test.c`:

1. exact v1 ops prefix through `.next` initializes successfully;
2. prefix one byte short is rejected without mutating zero reader;
3. wrong ABI is rejected without mutation;
4. NULL `.next` is rejected without mutation;
5. NULL context is accepted when callback supports it;
6. `next` OK commits only a valid temporary token into caller `out`;
7. invalid callback token -> `INVALID_TOKEN`, FAILED/sticky, caller `out` unchanged;
8. callback DONE -> DONE and no second provider invocation on repeated `next`;
9. each allowed reader callback error -> FAILED/sticky;
10. a disallowed known status such as `CSERDE_SINK_ERROR` -> `CALLBACK_ERROR`;
11. arbitrary unknown enum status -> `CALLBACK_ERROR`;
12. `reader == NULL` / `out == NULL` returns `INVALID_ARGUMENT` without advancing a READY reader;
13. calling `next` on ZERO returns `INVALID_STATE` without provider invocation.

Use a sentinel output to prove non-OK does not overwrite it:

```c
cserde_token out = { .kind = CSERDE_UINT, .value.uint = UINT64_C(0x1234) };
check_equal(cserde_reader_next(&reader, &out), CSERDE_SOURCE_ERROR);
check_true(out.kind == CSERDE_UINT);
check_equal(out.value.uint, UINT64_C(0x1234));
```

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset build-default-linux --target cserde_reader_test
```

Expected: link failure for `cserde_reader_init`/`cserde_reader_next`, not compile/configure failure.

Commit RED:

```bash
git add cserde/include/cserde cserde/tests
 git commit -m "test(cserde): define pull reader contract"
```

- [ ] **Step 3: Implement prefix validation and reader facade**

Add `src/reader.c` to the library in `cserde/CMakeLists.txt` and implement with a field-end prefix helper:

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

Do not require `ops->struct_size >= sizeof(cserde_reader_ops)`.

Reader initialization must construct a temporary facade and assign it only after all checks:

```c
cserde_reader next_reader;

if (reader == NULL || ops == NULL)
    return CSERDE_INVALID_ARGUMENT;
if (reader->ops != NULL || reader->context != NULL ||
    reader->state != CSERDE_READER_ZERO || reader->status != CSERDE_OK)
    return CSERDE_INVALID_STATE;
if (!cserde_reader_ops_valid(ops))
    return CSERDE_INVALID_ARGUMENT;

next_reader.ops = ops;
next_reader.context = context;
next_reader.state = CSERDE_READER_READY;
next_reader.status = CSERDE_OK;
*reader = next_reader;
return CSERDE_OK;
```

`cserde_reader_next` must call the provider with a local temporary token, validate it, and copy to caller output only on success. Allowed callback errors are forwarded into sticky FAILED state; DONE becomes sticky DONE; every other callback result becomes `CALLBACK_ERROR`.

- [ ] **Step 4: Run reader GREEN plus Task 1 regression**

```bash
cmake --build --preset build-default-linux --target cserde_token_test cserde_reader_test
ctest --preset test-release-linux -R '^(cserde_token_test|cserde_reader_test)$' --output-on-failure
```

Expected: both PASS.

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
- Consumes: `cserde_reader_next` and token grammar from Tasks 1–2.
- Produces: working `cserde_reader_skip_value(cserde_reader *, size_t max_depth)` with no allocation and exact one-value consumption.

- [ ] **Step 1: Add RED tests for scalar, nested ARRAY, arbitrary-key MAP, malformed structure, and depth**

Use a fixed token-array provider in the test file for this RED cycle:

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

Required concrete sequences:

```text
scalar:
  SINT(7), UINT(9)
  skip once -> next returns UINT(9)

nested array:
  ARRAY_BEGIN, SINT(1), ARRAY_BEGIN, BOOL(true), ARRAY_END, ARRAY_END, UINT(9)
  skip(max_depth=2) -> next UINT(9)

arbitrary MAP key:
  MAP_BEGIN, ARRAY_BEGIN, SINT(1), ARRAY_END, STRING("value"), MAP_END, NULL
  skip(max_depth=2) -> next NULL

mismatched end:
  ARRAY_BEGIN, SINT(1), MAP_END
  -> INVALID_TOKEN + FAILED

orphan map key:
  MAP_BEGIN, STRING("key"), MAP_END
  -> INVALID_TOKEN + FAILED

initial DONE:
  empty stream
  -> UNEXPECTED_END + FAILED

unterminated container:
  ARRAY_BEGIN, SINT(1), then DONE
  -> UNEXPECTED_END + FAILED

depth:
  ARRAY_BEGIN, ARRAY_BEGIN, NULL, ARRAY_END, ARRAY_END
  max_depth=1 -> LIMIT_EXCEEDED + FAILED
  max_depth=2 -> OK

zero depth:
  scalar with max_depth=0 -> OK
  ARRAY_BEGIN with max_depth=0 -> LIMIT_EXCEEDED
```

For every failed skip, call `cserde_reader_next` afterward and assert the same sticky failure without another provider call.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset build-default-linux --target cserde_reader_test
```

Expected: link failure on `cserde_reader_skip_value` if still undefined, or failing assertions if a temporary stub exists. Do not implement the walker before observing this RED.

Commit RED:

```bash
git add cserde/tests/cserde_reader_test.c
 git commit -m "test(cserde): define nested skip semantics"
```

- [ ] **Step 3: Implement one-value grammar walking in `reader.c`**

Use internal helpers that accept an already-read token so ARRAY/MAP END tokens are interpreted by their owning container loop rather than requiring pushback:

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

`take_token` wraps `cserde_reader_next`; when it observes `CSERDE_DONE` while a complete value is required, overwrite terminal state with:

```c
reader->state = CSERDE_READER_FAILED;
reader->status = CSERDE_UNEXPECTED_END;
return CSERDE_UNEXPECTED_END;
```

For ARRAY_BEGIN:

```c
if (depth >= max_depth)
    return cserde_reader_fail(reader, CSERDE_LIMIT_EXCEEDED);
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

For MAP_BEGIN, read a possible key/`MAP_END`, recursively consume the key value, then require and recursively consume exactly one value. Encountering ARRAY_END/MAP_END in the value position is `INVALID_TOKEN`. Canonical map keys remain any valid canonical value.

Top-level call reads one token and calls `skip_token(..., depth=0, max_depth)`. END as the first token is `INVALID_TOKEN`.

- [ ] **Step 4: Run all reader tests GREEN**

```bash
cmake --build --preset build-default-linux --target cserde_reader_test
ctest --preset test-release-linux -R '^cserde_reader_test$' --output-on-failure
```

Expected: PASS with exact one-value consumption and sticky failures.

- [ ] **Step 5: Commit Task 3 GREEN**

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
- Consumes: Task 1 token validation/status model.
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

D1 writer intentionally has no `abort` and no canonical nesting stack.

- [ ] **Step 1: Add writer declarations and RED tests before implementation**

Fake provider:

```c
typedef struct fake_writer_context {
    cserde_status write_status;
    cserde_status finish_status;
    size_t write_calls;
    size_t finish_calls;
    cserde_token observed;
} fake_writer_context;

static cserde_status fake_writer_write(void *context,
                                       const cserde_token *token) {
    fake_writer_context *state = (fake_writer_context *)context;
    ++state->write_calls;
    state->observed = *token;
    return state->write_status;
}

static cserde_status fake_writer_finish(void *context) {
    fake_writer_context *state = (fake_writer_context *)context;
    ++state->finish_calls;
    return state->finish_status;
}
```

Required tests:

1. exact ops prefix through `.finish` initializes;
2. one-byte-short prefix, bad ABI, NULL write, NULL finish reject without mutating zero writer;
3. valid token is forwarded exactly once;
4. invalid STRING/BYTES token returns `INVALID_TOKEN`, does not call provider, and leaves READY writer usable;
5. NULL writer/token preconditions do not call provider and do not poison READY writer;
6. allowed write callback errors (`VALUE_OUT_OF_RANGE`, `LIMIT_EXCEEDED`, `UNSUPPORTED`, `SINK_ERROR`) become FAILED/sticky;
7. disallowed known status (`SOURCE_ERROR` or `DONE`) becomes `CALLBACK_ERROR`;
8. unknown status becomes `CALLBACK_ERROR`;
9. successful `finish` -> FINISHED and exactly one finish callback;
10. repeated finish and write-after-finish return `INVALID_STATE` without callbacks;
11. failed finish is FAILED/sticky;
12. byte sink typedef compiles with a function matching `(void *, const void *, size_t)`.

- [ ] **Step 2: Run writer RED**

```bash
cmake --build --preset build-default-linux --target cserde_writer_test
```

Expected: link failure for writer facade symbols.

Commit RED:

```bash
git add cserde/include/cserde cserde/tests
 git commit -m "test(cserde): define push writer contract"
```

- [ ] **Step 3: Implement writer ABI/state facade**

Add `src/writer.c` to `turbo_cserde`. Validate prefix through `.finish` using the same field-end rule as reader. Init uses temporary-then-commit semantics.

`cserde_writer_write` flow must be exactly:

```text
validate caller pointers/state
  -> shallow cserde_token_valid
  -> call provider write
  -> OK: remain READY
  -> allowed provider error: FAILED/sticky
  -> all other callback status: CALLBACK_ERROR/FAILED
```

An invalid caller token is not a stream/provider failure and therefore leaves writer READY.

`cserde_writer_finish` calls provider only from READY; callback OK transitions to FINISHED, accepted errors to FAILED, all other results to CALLBACK_ERROR/FAILED.

Do not add container depth, map parity, abort, allocation, or serialization logic in `writer.c`.

- [ ] **Step 4: Run writer GREEN plus CSerde regression suite**

```bash
cmake --build --preset build-default-linux --target cserde_token_test cserde_reader_test cserde_writer_test
ctest --preset test-release-linux -R '^cserde_' --output-on-failure
```

Expected: all current CSerde tests PASS.

- [ ] **Step 5: Commit Task 4 GREEN**

```bash
git add cserde
 git commit -m "feat(cserde): add versioned push writer"
```

---

### Task 5: Test-only recording providers and C++17 public-header contract

**Files:**
- Create: `cserde/tests/support/recording.h`
- Create: `cserde/tests/support/recording.c`
- Create: `cserde/tests/cserde_recording_test.c`
- Create: `cserde/tests/cserde_header_cpp_test.cpp`
- Modify: `cserde/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: public reader/writer/token API.
- Produces test-only support, not installed/exported:

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

Provider semantics:

```text
recording reader: token[index++] -> OK; index==count -> DONE
recording writer: count<capacity -> shallow-copy token + OK; else LIMIT_EXCEEDED
recording finish: finished=false -> set true + OK; repeated direct provider finish is INVALID_STATE only inside support, though public facade prevents the second call
```

STRING/BYTES remain shallow borrowed slices; support never allocates or copies payload bytes.

- [ ] **Step 1: Add recording tests before support implementation**

Register `cserde_recording_test` with `support/recording.c` as a source only after the RED proof. First write the test and compile it against a declaration-only `recording.h`.

Concrete tests:

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

Verify:

1. reader emits exactly all four tokens then DONE;
2. writer with capacity 4 records all four shallow tokens and finish succeeds;
3. recorded STRING slice points at `name` rather than an allocated copy;
4. writer with capacity 3 fails fourth write with `LIMIT_EXCEEDED`, facade becomes FAILED, count remains 3;
5. no support API exposes allocation/free or ownership transfer.

- [ ] **Step 2: Run recording RED**

Compile the target before `recording.c` definitions exist.

```bash
cmake --build --preset build-default-linux --target cserde_recording_test
```

Expected: link failure on `cserde_recording_reader_ops` / `cserde_recording_writer_ops`.

Commit RED:

```bash
git add cserde/tests
 git commit -m "test(cserde): define recording provider harness"
```

- [ ] **Step 3: Implement recording providers with fixed caller-owned arrays**

`recording.c` must use only index/capacity checks and shallow token assignment. No `malloc`, `calloc`, `realloc`, `free`, `strdup`, TurboSTL, or Core utility calls.

Set ops prefixes to exact v1 field ends, not blindly to a future larger struct contract:

```c
const cserde_reader_ops cserde_recording_reader_ops = {
    .struct_size = offsetof(cserde_reader_ops, next) +
                   sizeof(((cserde_reader_ops *)0)->next),
    .abi_version = CSERDE_READER_OPS_ABI_VERSION,
    .next = cserde_recording_reader_next
};
```

Use analogous `.finish` field-end sizing for writer ops.

- [ ] **Step 4: Add C++17 public-header conformance test**

`cserde_header_cpp_test.cpp` includes only:

```cpp
#include <cserde/cserde.h>
#include "tinytest.h"

#include <cstddef>
#include <type_traits>
```

Compile-time assertions:

```cpp
static_assert(std::is_standard_layout<cserde_slice>::value, "slice ABI");
static_assert(std::is_standard_layout<cserde_token>::value, "token ABI");
static_assert(std::is_standard_layout<cserde_reader_ops>::value, "reader ops ABI");
static_assert(std::is_standard_layout<cserde_reader>::value, "reader facade ABI");
static_assert(std::is_standard_layout<cserde_writer_ops>::value, "writer ops ABI");
static_assert(std::is_standard_layout<cserde_writer>::value, "writer facade ABI");
static_assert(CSERDE_READER_OPS_ABI_VERSION == 1u, "reader ABI version");
static_assert(CSERDE_WRITER_OPS_ABI_VERSION == 1u, "writer ABI version");
```

Runtime TinyTest case constructs prefix-sized reader/writer ops and verifies both init calls succeed from C++17.

Register target with:

```cmake
set_target_properties(cserde_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
```

- [ ] **Step 5: Run recording and C++ GREEN**

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

### Task 6: Contract hardening, full exact-head verification, and PR readiness

**Files:**
- Modify only files identified by review failures; no new feature scope.
- Verify: all D1 files, `CMakeLists.txt`, `.github/workflows/cmeta.yml`.

**Interfaces:**
- Consumes: complete D1 surface from Tasks 1–5.
- Produces: review-clean exact head suitable for merge; no CBind or format adapter code.

- [ ] **Step 1: Run static repository-scope checks**

Search production `cserde` for forbidden dependencies/implementation creep:

```bash
rg -n "cmeta|cflow|turbostl|turbo_parser|TurboParser|json|yaml|xml|csv|malloc|calloc|realloc|free" cserde/include cserde/src cserde/CMakeLists.txt
```

Expected: no dependency includes/links, parser implementation, or allocation calls. Mentions in comments should be removed unless they state a necessary contract; production code must have none of those dependencies.

Check workflow wiring:

```bash
rg -n "cserde" CMakeLists.txt .github/workflows/cmeta.yml cserde/CMakeLists.txt cserde/tests/CMakeLists.txt
```

Expected: root subdirectory, workflow path filters/test regex, module target, and test targets are present.

- [ ] **Step 2: Run format/diff checks**

```bash
git diff --check master...HEAD
```

If repository clang-format gate is available, run it only over changed C/C++ files using the repository `.clang-format`; fix formatting without semantic changes.

- [ ] **Step 3: Fresh Linux release verification**

Delete/recreate the preset build tree as the repository preset workflow does, then:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux -R '^(cmeta_|cserde_|cflow_|turbostl_)' --output-on-failure
```

Expected: configure PASS, full build PASS, all selected tests PASS.

- [ ] **Step 4: Fresh Windows/MSVC release verification**

Use the same commands as `.github/workflows/cmeta.yml` after `VsDevCmd.bat`:

```bat
cmake --preset release-win-msvc-ninja
cmake --build --preset build-release-windows
ctest --preset test-release-windows -R "^(cmeta_|cserde_|cflow_|turbostl_)" --output-on-failure
```

Expected: configure/build/test PASS.

- [ ] **Step 5: Review exact-head behavior against every spec acceptance item**

The final review must explicitly verify all 18 spec acceptance items:

1. token/status model exists with no format-specific token;
2. SINT/UINT/FLOAT storage widths are exact;
3. STRING/BYTES slice validation and both lifetimes behave as specified;
4. reader ops prefix/ABI validation is append-safe;
5. reader init failure leaves zero facade unchanged;
6. reader non-OK never overwrites caller output;
7. DONE/FAILED reader state is sticky;
8. allowed reader errors propagate and disallowed statuses normalize to CALLBACK_ERROR;
9. `skip_value` crosses nested arrays/maps and consumes exactly one value;
10. malformed END/map parity/early DONE/depth failures poison reader with correct status;
11. writer ops prefix/ABI validation is append-safe;
12. invalid caller token does not invoke provider or poison READY writer;
13. writer callback status normalization and terminal finish state are correct;
14. writer has no abort and no duplicate nesting stack;
15. recording support is fixed-capacity/no-allocation/test-only;
16. C++17 umbrella public include passes;
17. production target has no CMeta/CFlow/TurboSTL/Core/parser dependencies;
18. exact-head Linux + Windows release CI is green.

Any discovered defect gets one focused RED regression before the fix, then rerun affected CSerde tests.

- [ ] **Step 6: Commit review fixes, if any, then push exact head**

Use a focused commit message matching the defect, for example:

```bash
git commit -am "fix(cserde): preserve reader output on callback failure"
```

Do not create a catch-all refactor commit unless actual review changes require it.

- [ ] **Step 7: Open/update a draft PR and require exact-head CI**

PR title:

```text
feat(cserde): add canonical token protocol
```

PR body must record:

```text
D1 scope: canonical token + versioned reader/writer + skip_value + recording harness only.
No CBind, parser, TurboSTL, CMeta semantic, or container-construction production changes.
```

Include the exact final head SHA and the Linux/Windows workflow run that executed that SHA. Mark Ready for Review only after both jobs are successful.

---

## Expected Commit Shape

The intended development history is reviewable and TDD-visible:

```text
test(cserde): define canonical token contract        RED
feat(cserde): add canonical token model             GREEN
test(cserde): define pull reader contract            RED
feat(cserde): add versioned pull reader              GREEN
test(cserde): define nested skip semantics           RED
feat(cserde): add bounded value skipping             GREEN
test(cserde): define push writer contract            RED
feat(cserde): add versioned push writer              GREEN
test(cserde): define recording provider harness      RED
test(cserde): add recording and C++ conformance      GREEN
fix(cserde): <review defect>                         only if review finds one
```

The exact commit count may differ only when a platform/build failure requires an independently reviewable focused fix. Do not squash RED/GREEN evidence before review.
