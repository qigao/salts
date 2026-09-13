# TLog vstr ABI Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace TLog's borrowed C-string runtime ABI with bounded `vstr` fields and parameters, including async copying, sinks, decorators, filters, metrics, macros, and reflection.

**Architecture:** `vstr` becomes the canonical borrowed runtime string type for component, file, message, and format pattern. Async logging copies exactly the supplied view lengths into queue-owned storage; sinks consume those views directly. C strings remain only at NUL-based configuration/OS boundaries, and no compatibility ABI is retained.

**Tech Stack:** C11, C++17 integration tests, Salts `vstr`/`tstr`, CMeta reflection, TinyTest, CMake, GitHub Actions CMeta matrix.

**Spec:** `docs/superpowers/specs/2026-09-13-tlog-vstr-abi-design.md`

## Global Constraints

- This phase intentionally breaks the existing public `tlog` ABI and source API.
- No `*_v` compatibility twins, legacy fallback ABI, or alias fields are added.
- Borrowed runtime strings use `vstr`; owned mutable construction uses `tstr`.
- `const char *` remains only for genuine NUL-based boundaries such as paths or construction-time options.
- The first production change is forbidden until the test-only ABI contract is observed RED on exact-head CI.
- Final acceptance requires C API notation plus full CMeta conformance GREEN on Linux, macOS, Windows, and Android.

---

### Task 1: Establish the test-only RED contract

**Files:**
- Modify: `utils/tests/test_tlog.c`
- Modify: `utils/tests/test_tlog_cpp.cpp`

**Interfaces:**
- Consumes: current pre-migration `salts_log_entry_t`, `salts_log_str`, `salts_log_typed`.
- Produces: compile-time/runtime tests that require the new `vstr` ABI and therefore fail against the current implementation.

- [ ] **Step 1: Replace the legacy-layout assertions with the intended new reflected ABI contract**

In `utils/tests/test_tlog.c`, remove `salts_log_entry_legacy_layout`, the old size/alignment/offset assertions, and the 8-field metadata expectation. Replace them with checks for exactly seven fields:

```c
const char *names[] = {
    "level", "timestamp_ms", "thread_id", "component",
    "file", "line", "message"};
const char *types[] = {
    "salts_log_level_t", "uint64_t", "uint32_t", "vstr",
    "vstr", "int", "vstr"};
check_equal(meta->field_count, (size_t)7);
check_null(cmeta_struct_find_field(meta, "message_len"));
```

In `utils/tests/test_tlog_cpp.cpp`, require `field_count == 7`, require the final field offset to be `offsetof(salts_log_entry_t, message)`, and remove all `message_len` references.

- [ ] **Step 2: Add bounded raw-view callback coverage**

Add callback capture state using byte arrays plus explicit lengths, and add a test that calls the future ABI directly:

```c
const char component_raw[] = {'c','o','m','p'};
const char file_raw[] = {'f','.','c'};
const char message_raw[] = {'h','e','l','l','o'};
salts_log_str(logger, SALTS_LOG_LEVEL_INFO,
              vstr_from_buf(component_raw, sizeof(component_raw)),
              vstr_from_buf(file_raw, sizeof(file_raw)), 17,
              vstr_from_buf(message_raw, sizeof(message_raw)));
```

The callback must compare `.data/.len` with `memcmp`, never `%s`/`strcmp`.

- [ ] **Step 3: Add async-retention coverage**

Publish component/file/message views backed by mutable local arrays, mutate those arrays immediately after `salts_log_str` returns, call `tlog_flush`, and verify the callback received the original bytes. This proves the async queue copied by length before returning.

- [ ] **Step 4: Add bounded typed-pattern coverage**

Use a non-NUL-terminated pattern such as:

```c
const char pattern_raw[] = {'[','{','}',']'};
fmt_arg_t arg = fmt_arg_int(42);
salts_log_typed(logger, SALTS_LOG_LEVEL_INFO,
                VSTR_LIT("typed"), (vstr){NULL, 0}, 0,
                vstr_from_buf(pattern_raw, sizeof(pattern_raw)),
                &arg, 1U);
```

Verify the callback receives `[42]` as a bounded message view.

- [ ] **Step 5: Add decorator and metrics bounded-length tests**

Add one test where `salts_sink_format_create` forwards a formatted message and the inner callback checks `entry->message.len`; add one metrics test that proves `bytes_forwarded` equals the sum of `message.len` values.

- [ ] **Step 6: Commit the RED contract only**

```bash
git add utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp
git commit -m "test: require vstr tlog runtime ABI"
```

- [ ] **Step 7: Verify exact-head RED before touching production code**

Run focused local/CI build if available:

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp --parallel
ctest --preset linux-release-user -R "^(test_tlog|test_tlog_cpp)$" --output-on-failure
```

Expected: compile failure on the old `salts_log_entry_t` fields/signatures (`vstr` arguments incompatible with `const char *`, missing `.len` on C-string fields, or stale `message_len`). Record the exact-head CMeta run ID and failing host jobs.

---

### Task 2: Break the public TLog ABI cleanly

**Files:**
- Modify: `utils/include/tlog.h`
- Modify: `utils/tests/test_tlog.c`
- Modify: `utils/tests/test_tlog_cpp.cpp`

**Interfaces:**
- Consumes: `vstr` from `str.h`/`vstr.h` and `fmt_arg_t` from `fmt.h`.
- Produces:
  - `salts_log_entry_t { ... vstr component; vstr file; ... vstr message; }`
  - `void salts_log_str(..., vstr component, vstr file, int line, vstr message)`
  - `void salts_log_typed(..., vstr component, vstr file, int line, vstr pattern, const fmt_arg_t *, size_t)`

- [ ] **Step 1: Change `salts_log_entry_t` to seven fields**

Use the exact struct from the spec and delete `message_len`.

- [ ] **Step 2: Replace the public raw and typed signatures**

Do not add parallel names or compatibility wrappers.

- [ ] **Step 3: Define source-capture view macros**

Add view-valued macros, for example:

```c
#if SALTS_LOG_CAPTURE_SOURCE
  #define SALTS_LOG_SOURCE_FILE_VIEW VSTR_LIT(__FILE__)
#else
  #define SALTS_LOG_SOURCE_FILE_VIEW ((vstr){NULL, 0})
#endif
```

Keep path/pattern sink option structs unchanged in this task.

- [ ] **Step 4: Build the focused tests and confirm failures have moved into implementation/call sites**

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp --parallel
```

Expected: the header-level ABI tests now compile further, but production `tlog.c` and stale call sites fail on old pointer/`message_len` assumptions.

- [ ] **Step 5: Commit the ABI header break**

```bash
git add utils/include/tlog.h utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp
git commit -m "refactor: make tlog entry ABI view-native"
```

---

### Task 3: Convert producer and async queue ownership to `vstr`

**Files:**
- Modify: `utils/src/tlog.c`
- Modify: `utils/include/tlog.h`

**Interfaces:**
- Consumes: new public signatures from Task 2.
- Produces: async records that store `vstr component/file/message` pointing into queue-owned `data[]`; raw and typed calls reject invalid views and copy before returning.

- [ ] **Step 1: Replace `async_log_entry_t` string members**

Use:

```c
typedef struct {
  salts_log_level_t level;
  uint64_t timestamp_ms;
  uint32_t thread_id;
  int line;
  vstr component;
  vstr file;
  vstr message;
  char data[];
} async_log_entry_t;
```

- [ ] **Step 2: Rewrite `async_entry_create` to use supplied lengths directly**

Compute `total_size` from `entry->component.len`, `entry->file.len`, and `entry->message.len`. Copy exactly those byte counts, retain any padding only outside the logical view, and set each stored `vstr` to the copied range. Do not call `strlen` or `vstr_from_cstr` here.

- [ ] **Step 3: Update sync/async reconstruction to assign views directly**

Every temporary/public `salts_log_entry_t` must copy the three `vstr` values, not pointer-plus-length pairs.

- [ ] **Step 4: Rewrite `salts_log_str` validation and entry construction**

Reject any argument for which `!vstr_is_valid(view)`. Construct:

```c
salts_log_entry_t entry = {
  .level = level,
  .timestamp_ms = now_ms,
  .thread_id = get_cached_tid(),
  .component = component,
  .file = file,
  .line = line,
  .message = message,
};
```

- [ ] **Step 5: Rewrite `salts_log_typed` to call bounded formatter APIs**

Validate all views, render `pattern` through `fmt_print_v`, then pass `vstr_from_buf(tls_msg_buf, (size_t)msg_len)` to `salts_log_str`. Never convert the pattern back to a C string.

- [ ] **Step 6: Run focused tests**

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp --parallel
ctest --preset linux-release-user -R "^(test_tlog|test_tlog_cpp)$" --output-on-failure
```

Expected: raw/typed/async-retention contract tests progress to sink/decorator failures rather than producer ABI failures.

- [ ] **Step 7: Commit**

```bash
git add utils/src/tlog.c utils/include/tlog.h
git commit -m "refactor: copy tlog views by length"
```

---

### Task 4: Convert sinks, decorators, filters, and metrics

**Files:**
- Modify: `utils/src/tlog.c`
- Modify: `utils/tests/test_tlog.c`

**Interfaces:**
- Consumes: view-native `salts_log_entry_t`.
- Produces: no runtime sink path requires component/file/message NUL termination.

- [ ] **Step 1: Rewrite pattern rendering**

For component/file/message tokens, copy `entry->X.data` for `entry->X.len` bytes after buffer-capacity checks. Remove `vstr_from_cstr(entry->component)` and `vstr_from_cstr(entry->file)`.

- [ ] **Step 2: Rewrite callback/custom sink tests and code paths**

Replace `%s`, `snprintf(..., "%s", entry->message)`, `strcmp(entry->component, ...)`, and pointer-null checks with length-aware operations.

- [ ] **Step 3: Make component filtering length-aware**

Construction may retain an owned/canonical C string for `opts->component`, but runtime matching must compare its known length to `entry->component.len` and use `memcmp` only for equal lengths.

- [ ] **Step 4: Rewrite format decorator output**

When producing a temporary formatted message, set:

```c
formatted_entry.message = vstr_from_buf(formatted, (size_t)len);
```

Do not manufacture a `message_len` side channel.

- [ ] **Step 5: Rewrite metrics accounting**

Use `entry->message.len` for `bytes_forwarded`.

- [ ] **Step 6: Run focused TLog tests**

```bash
cmake --build --preset linux-release-user --target test_tlog --parallel
ctest --preset linux-release-user -R '^test_tlog$' --output-on-failure
```

Expected: all C TLog tests PASS, including raw non-NUL, async-retention, decorator, filter, and metrics cases.

- [ ] **Step 7: Commit**

```bash
git add utils/src/tlog.c utils/tests/test_tlog.c
git commit -m "refactor: consume tlog entry views in sinks"
```

---

### Task 5: Rebuild convenience macros and downstream C/C++ call sites

**Files:**
- Modify: `utils/include/tlog.h`
- Modify: `utils/tests/test_tlog.c`
- Modify: `utils/tests/test_tlog_cpp.cpp`
- Modify: `utils/examples/cpp_test.cpp`
- Modify any repository call site reported by code search/build that calls `salts_log_str` or `salts_log_typed` directly.

**Interfaces:**
- Consumes: view-native primitives.
- Produces: ergonomic literal/C-string macro surface with single evaluation and no hidden ABI fallback.

- [ ] **Step 1: Update raw macros**

Evaluate component/message expressions once. Convert general C-string expressions once with `vstr_from_cstr`; use `SALTS_LOG_SOURCE_FILE_VIEW` for source capture. Preserve existing level checks before conversion/log publication.

- [ ] **Step 2: Update formatted macros**

Adapt component and format-string expressions at the macro edge, then call the new `salts_log_typed` signature. Keep all existing `FMT_ARGS(...)` typing behavior.

- [ ] **Step 3: Update direct repository call sites**

Search:

```bash
rg -n "salts_log_(str|typed)\(" .
```

For each direct caller, pass explicit `vstr` values. Do not add an old-signature helper.

- [ ] **Step 4: Preserve C++ integration behavior**

`test_tlog_cpp.cpp` and `utils/examples/cpp_test.cpp` must continue to compile under C++17 with `std::string`, `std::string_view`, enums, and helpers. The ABI break is in TLog runtime strings, not `fmt_arg_t` support.

- [ ] **Step 5: Run focused C and C++ tests**

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp cpp_test_tlog --parallel
ctest --preset linux-release-user -R "^(test_tlog|test_tlog_cpp)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add utils/include/tlog.h utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp utils/examples/cpp_test.cpp
git add -u
git commit -m "refactor: adapt tlog call sites to vstr ABI"
```

---

### Task 6: Update reflection and reject stale C-string assumptions

**Files:**
- Modify: `utils/tests/test_tlog.c`
- Modify: `utils/tests/test_tlog_cpp.cpp`
- Modify CMeta/consumer tests only if exact failures show stale `salts_log_entry_t` expectations.

**Interfaces:**
- Consumes: seven-field reflected `salts_log_entry_t`.
- Produces: reflection contract where `component`, `file`, and `message` are `vstr` and `message_len` is absent.

- [ ] **Step 1: Assert exact metadata names/types/sizes/alignments**

The three view fields must report type name `vstr`, size `sizeof(vstr)`, and alignment `CMETA_ALIGNOF(vstr)`.

- [ ] **Step 2: Search for stale ABI assumptions**

```bash
rg -n "message_len|const char \*.*(component|file|message)|salts_log_entry_t" cmeta utils cflow cflow-scxml
```

Only update consumers that materially depend on the old TLog entry ABI.

- [ ] **Step 3: Run focused reflection/C++ tests**

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp --parallel
ctest --preset linux-release-user -R "^(test_tlog|test_tlog_cpp)$" --output-on-failure
```

Expected: PASS with seven-field metadata.

- [ ] **Step 4: Commit**

```bash
git add utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp
git add -u
git commit -m "test: lock tlog vstr reflection contract"
```

---

### Task 7: Exact-head verification and PR evidence

**Files:**
- Modify: PR body only after exact-head results exist.

**Interfaces:**
- Consumes: complete migration.
- Produces: merge evidence; no production changes.

- [ ] **Step 1: Run local focused verification**

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp --parallel
ctest --preset linux-release-user -R "^(test_tlog|test_tlog_cpp)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 2: Run diff hygiene**

```bash
git diff --check master...HEAD
rg -n "message_len" utils/include/tlog.h utils/src/tlog.c utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp
```

Expected: no whitespace errors and no runtime `message_len` dependency.

- [ ] **Step 3: Push exact head and observe C API notation**

Expected: GREEN. Do not weaken notation checks.

- [ ] **Step 4: Observe full CMeta conformance**

Require GREEN on:
- Linux release, including full Linux suite and CFlow ASan lifecycle;
- macOS 15 release;
- Windows release;
- Android arm64 Release cross-build.

- [ ] **Step 5: If any platform is RED, fix only the evidenced failure and rerun exact-head gates**

Do not add compatibility fields/signatures to make consumers compile. Migrate stale consumers to the new ABI.

- [ ] **Step 6: Update the PR body with RED→GREEN evidence**

Record the test-only RED commit/run, final exact head SHA, C API notation run ID, CMeta conformance run ID, and all platform conclusions.

- [ ] **Step 7: Final review**

Verify the acceptance criteria from the spec line-by-line: seven-field view-native entry, no `message_len`, bounded raw/typed inputs, async copied ownership, length-aware sinks/filters/decorators/metrics, no compatibility ABI, all exact-head gates GREEN.
