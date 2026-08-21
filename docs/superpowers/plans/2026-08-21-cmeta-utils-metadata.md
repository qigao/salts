# CMeta Utils Metadata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose read-only CMeta metadata for `fmt_type_t` and `turbo_log_entry_t` while preserving the existing fmt/tlog API, ABI, ownership, output, and hot paths.

**Architecture:** Add shared header-generation attributes and cross-language alignment spelling to `cmeta/pp.h`, then reuse them from CMeta's generated Enum/Struct/Interface/Container surfaces. Extend fmt's existing storage Schema to generate enum metadata, and express the unchanged log-entry layout through CMeta `Struct(...)`; runtime formatting and logging implementations remain untouched.

**Tech Stack:** C11, C++17 consumer headers, CMeta `Schema/Replay`, CMeta Enum/Struct descriptors, TurboUtils fmt/tlog, TinyTest, CMake Presets, MSVC Release, Clang strict-warning syntax checks.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-utils-metadata-design.md`

## Global Constraints

- Preserve `FMT_TYPE_NONE == 0`, all existing fmt tag values `1..14`, every `fmt_arg_t` union member, and the full `fmt_arg_t` size/alignment/layout.
- Preserve `turbo_log_level_t` values `0..4` and every `turbo_log_entry_t` field type, order, offset, size, and alignment.
- Preserve `FMT_ARG`, `FMT_ARGS`, `FMT_WRAP_0..8`, `fmt`, `TLOG_*`, `TURBO_LOG_*`, exported functions, sink callbacks, errors, allocations, ownership, and output.
- Metadata is immutable and TU-local. Compare descriptor contents, never addresses across translation units.
- `component`, `file`, and `message` remain borrowed for the existing sink-callback lifetime.
- Do not modify `utils/src/fmt.c`, `utils/src/tlog.c`, string algorithms, queueing, sinks, or runtime registry unless a failing test proves the approved design impossible; stop for user review before expanding scope.
- Runtime latency regression over 10%, throughput regression over 10%, compile-time median regression over 10%, or Release size regression over 20% requires investigation and either rollback or explicit acceptance.
- The worktree contains overlapping staged user changes. Use `apply_patch`, inspect index and working-tree diffs separately, and do not commit implementation files. End each task with a path-scoped diff/check checkpoint.

## File Map

- `cmeta/include/cmeta/pp.h`: owns common generated-header linkage/diagnostic macros and portable alignof spelling.
- `cmeta/include/cmeta/enum.h`: uses the common macros for generic and generated enum metadata helpers.
- `cmeta/include/cmeta/struct.h`: uses the common macros and portable alignment for reflected structs.
- `cmeta/include/cmeta/interface.h`: replaces interface-local inline/unused definitions with the shared CMeta macros.
- `cmeta/include/cmeta/container.h`: replaces container-local inline/unused definitions with the shared CMeta macros.
- `cmeta/tests/cmeta_header_cpp_test.cpp`: verifies C++17 Struct metadata, `CMETA_ALIGNOF`, and strict public-header compilation.
- `cmeta/tests/CMakeLists.txt`: builds the new C++ CMeta header regression test.
- `utils/include/fmt.h`: extends the existing fmt storage Schema and generates public typed enum metadata helpers.
- `utils/tests/test_fmt.c`: verifies fmt metadata semantics and C ABI layout.
- `utils/tests/test_fmt_cpp.cpp`: verifies fmt metadata through the C++ public header.
- `utils/include/tlog.h`: declares the unchanged log entry with CMeta `Struct(...)`.
- `utils/tests/test_tlog.c`: verifies field metadata, borrowed-field declarations, and C ABI layout.
- `utils/tests/test_tlog_cpp.cpp`: verifies the reflected log entry from C++.

---

### Task 1: Common CMeta generated-header foundation

**Files:**
- Modify: `cmeta/include/cmeta/pp.h`
- Modify: `cmeta/include/cmeta/enum.h`
- Modify: `cmeta/include/cmeta/struct.h`
- Modify: `cmeta/include/cmeta/interface.h`
- Modify: `cmeta/include/cmeta/container.h`
- Create: `cmeta/tests/cmeta_header_cpp_test.cpp`
- Modify: `cmeta/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `Schema`, `Replay`, `CMETA_INTERFACE`, and CMeta container generators.
- Produces: `CMETA_UNUSED`, `CMETA_INLINE`, `CMETA_LOCAL`, and `CMETA_ALIGNOF(type)` for Tasks 2 and 3.

- [ ] **Step 1: Capture the clean functional, compile-time, benchmark, and size baselines**

Build and test the current tree before adding RED tests:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test test_fmt test_fmt_cpp test_tlog test_tlog_cpp test_fmt_bench bench_tlog && ctest --preset win-release-user -R ""^(cmeta_core_test|test_fmt|test_fmt_cpp|test_tlog|test_tlog_cpp)$"" --output-on-failure"
```

Run both benchmark executables three times. Record the median `avg(us)` and `ops/s` for fmt rows `single int`, `3 args mixed`, and `8 args`; and tlog rows `sync_formatted_message`, `async_enqueue_latency`, and `filtered_out_message` in the task notes:

```powershell
1..3 | ForEach-Object { .\build\Msvc-Release\bin\test_fmt_bench.exe }
1..3 | ForEach-Object { .\build\Msvc-Release\bin\bench_tlog.exe }
```

Record five compile samples and their median for representative C and C++ consumers:

```powershell
$commands = @(
  @{ Name = "fmt_c"; Command = { clang -std=c11 -fsyntax-only -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_fmt.c } },
  @{ Name = "fmt_cpp"; Command = { clang++ -std=c++17 -fsyntax-only -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_fmt_cpp.cpp } },
  @{ Name = "tlog_c"; Command = { clang -std=c11 -fsyntax-only -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_tlog.c } },
  @{ Name = "tlog_cpp"; Command = { clang++ -std=c++17 -fsyntax-only -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_tlog_cpp.cpp } }
)
foreach ($entry in $commands) {
  $samples = 1..5 | ForEach-Object {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    & $entry.Command
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $sw.Stop()
    [math]::Round($sw.Elapsed.TotalMilliseconds, 3)
  }
  $sorted = $samples | Sort-Object
  "$($entry.Name): samples_ms=$($samples -join ',') median_ms=$($sorted[2])"
}
```

Record Release sizes:

```powershell
Get-Item build/Msvc-Release/bin/turbo_utils.dll,
         build/Msvc-Release/bin/test_fmt.exe,
         build/Msvc-Release/bin/test_fmt_cpp.exe,
         build/Msvc-Release/bin/test_tlog.exe,
         build/Msvc-Release/bin/test_tlog_cpp.exe |
  Select-Object Name, Length
```

Expected: focused tests pass; baselines have concrete numeric output. Do not compare benchmark runs performed concurrently with another build.

- [ ] **Step 2: Add the failing C++ generated-header test**

Create `cmeta/tests/cmeta_header_cpp_test.cpp`:

```cpp
#include <cmeta/meta.h>
#include "tinytest.hpp"

#include <cstddef>

Struct(cmeta_cpp_record,
    (int, value),
    (const char *, name)
);

static_assert(CMETA_ALIGNOF(cmeta_cpp_record) == alignof(cmeta_cpp_record),
              "CMETA_ALIGNOF must use the active language spelling");

spec("CMeta C++ public headers") {
  it("reflects struct fields through the C++ public surface") {
    const cmeta_struct_desc *meta = cmeta_cpp_record_meta();

    check_not_null(meta);
    check_equal(meta->name, "cmeta_cpp_record");
    check_equal(meta->field_count, static_cast<size_t>(2));
    check_equal(meta->fields[0].offset, offsetof(cmeta_cpp_record, value));
    check_equal(meta->fields[1].offset, offsetof(cmeta_cpp_record, name));
  }
}
```

Add this target to `cmeta/tests/CMakeLists.txt` after `cmeta_core_test`:

```cmake
cmake_add_test(cmeta_header_cpp_test
  SOURCES cmeta_header_cpp_test.cpp
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

set_target_properties(cmeta_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
```

- [ ] **Step 3: Run the RED test and verify the failure reason**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_header_cpp_test"
```

Expected: compile failure because `CMETA_ALIGNOF` does not exist and/or `Struct(...)` expands C11-only `_Alignof` in C++. A linker or unrelated include failure is not the intended RED and must be corrected before production edits.

- [ ] **Step 4: Add the shared CMeta macros**

Add this block near the top of `cmeta/include/cmeta/pp.h`, after the include guard and before the preprocessor iteration kernel:

```c
#if defined(__GNUC__) || defined(__clang__)
#define CMETA_UNUSED __attribute__((unused))
#else
#define CMETA_UNUSED
#endif

#define CMETA_INLINE static inline CMETA_UNUSED
#define CMETA_LOCAL static CMETA_UNUSED

#ifdef __cplusplus
#define CMETA_ALIGNOF(type) alignof(type)
#else
#define CMETA_ALIGNOF(type) _Alignof(type)
#endif
```

Apply these exact mechanical changes:

- In `container.h`, delete `CMETA_CONTAINER_UNUSED`, `CMETA_CONTAINER_INLINE`, and `CMETA_CONTAINER_LOCAL`; replace all `CMETA_CONTAINER_INLINE` with `CMETA_INLINE` and all `CMETA_CONTAINER_LOCAL` with `CMETA_LOCAL`.
- In `interface.h`, delete `CMETA_IFACE_UNUSED` and `CMETA_IFACE_INLINE`; replace all `CMETA_IFACE_INLINE` with `CMETA_INLINE`. Change generated method metadata, interface descriptors, concrete vtables, and other generated `static const` declarations to `CMETA_LOCAL const`.
- In `enum.h`, replace generic and generated `static inline` helpers with `CMETA_INLINE`, and generated metadata arrays/descriptors with `CMETA_LOCAL const`.
- In `struct.h`, replace generic and generated `static inline` helpers with `CMETA_INLINE`, generated metadata arrays/descriptors with `CMETA_LOCAL const`, and both `_Alignof(type)` spellings with `CMETA_ALIGNOF(type)`.

Do not rename generated functions or descriptor variables.

- [ ] **Step 5: Run GREEN CMeta validation**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test cmeta_header_cpp_test && ctest --preset win-release-user -R ""^(cmeta_core_test|cmeta_header_cpp_test)$"" --output-on-failure"
clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I tinytest/include cmeta/tests/cmeta_header_cpp_test.cpp
```

Expected: 2/2 CMeta tests pass and Clang C++ emits no warnings.

- [ ] **Step 6: Check the Task 1 patch without committing overlapping user work**

```powershell
git diff --check -- cmeta/include/cmeta/pp.h cmeta/include/cmeta/enum.h cmeta/include/cmeta/struct.h cmeta/include/cmeta/interface.h cmeta/include/cmeta/container.h cmeta/tests/CMakeLists.txt cmeta/tests/cmeta_header_cpp_test.cpp
git diff HEAD -- cmeta/include/cmeta/pp.h cmeta/include/cmeta/enum.h cmeta/include/cmeta/struct.h cmeta/include/cmeta/interface.h cmeta/include/cmeta/container.h cmeta/tests/CMakeLists.txt cmeta/tests/cmeta_header_cpp_test.cpp
```

Expected: only the approved shared-macro, cross-language alignment, warning-cleanup, and C++ regression changes. Do not commit these paths because some already contain staged user changes.

---

### Task 2: Public fmt type metadata from the storage Schema

**Files:**
- Modify: `utils/include/fmt.h`
- Modify: `utils/tests/test_fmt.c`
- Modify: `utils/tests/test_fmt_cpp.cpp`

**Interfaces:**
- Consumes: `CMETA_INLINE`, `CMETA_LOCAL`, `cmeta_enum_desc`, and the existing fmt storage/detection schemas.
- Produces: `fmt_type_t_meta()`, `fmt_type_t_to_string()`, `fmt_type_t_to_symbol()`, and `fmt_type_t_from_string()`.

- [ ] **Step 1: Add the C RED metadata and ABI tests**

In `utils/tests/test_fmt.c`, after the existing tag-value `_Static_assert`, add a test-only legacy mirror containing the current public layout:

```c
typedef struct fmt_arg_legacy_layout {
  fmt_type_t type;
  union {
    char c;
    int i;
    unsigned int u;
    long l;
    unsigned long ul;
    long long ll;
    unsigned long long ull;
    double f;
    const char *s;
    const void *p;
    size_t sz;
    int b;
    vstr sv;
    turbo_timeval_t tv;
  } val;
} fmt_arg_legacy_layout;

_Static_assert(sizeof(fmt_arg_t) == sizeof(fmt_arg_legacy_layout),
               "fmt_arg_t size changed");
_Static_assert(CMETA_ALIGNOF(fmt_arg_t) == CMETA_ALIGNOF(fmt_arg_legacy_layout),
               "fmt_arg_t alignment changed");
_Static_assert(offsetof(fmt_arg_t, type) == offsetof(fmt_arg_legacy_layout, type),
               "fmt_arg_t type offset changed");
_Static_assert(offsetof(fmt_arg_t, val) == offsetof(fmt_arg_legacy_layout, val),
               "fmt_arg_t value offset changed");
#define FMT_ASSERT_VALUE_MEMBER(member) \
  _Static_assert(offsetof(fmt_arg_t, val.member) == \
                     offsetof(fmt_arg_legacy_layout, val.member), \
                 "fmt_arg_t member offset changed: " #member); \
  _Static_assert(sizeof(((fmt_arg_t *)0)->val.member) == \
                     sizeof(((fmt_arg_legacy_layout *)0)->val.member), \
                 "fmt_arg_t member size changed: " #member)
FMT_ASSERT_VALUE_MEMBER(c);
FMT_ASSERT_VALUE_MEMBER(i);
FMT_ASSERT_VALUE_MEMBER(u);
FMT_ASSERT_VALUE_MEMBER(l);
FMT_ASSERT_VALUE_MEMBER(ul);
FMT_ASSERT_VALUE_MEMBER(ll);
FMT_ASSERT_VALUE_MEMBER(ull);
FMT_ASSERT_VALUE_MEMBER(f);
FMT_ASSERT_VALUE_MEMBER(s);
FMT_ASSERT_VALUE_MEMBER(p);
FMT_ASSERT_VALUE_MEMBER(sz);
FMT_ASSERT_VALUE_MEMBER(b);
FMT_ASSERT_VALUE_MEMBER(sv);
FMT_ASSERT_VALUE_MEMBER(tv);
#undef FMT_ASSERT_VALUE_MEMBER
```

Add this behavior inside `spec("FMT Tests")` before the formatting tests:

```c
it("should expose stable CMeta type metadata") {
  const cmeta_enum_desc *meta = fmt_type_t_meta();
  fmt_type_t parsed = FMT_TYPE_PTR;

  check_not_null(meta);
  check_equal(meta->name, "fmt_type_t");
  check_equal(meta->count, (size_t)15);
  check_equal(fmt_type_t_to_string(FMT_TYPE_NONE), "none");
  check_equal(fmt_type_t_to_string(FMT_TYPE_UINT), "uint");
  check_equal(fmt_type_t_to_symbol(FMT_TYPE_TIME), "FMT_TYPE_TIME");
  check_true(fmt_type_t_from_string("strv", &parsed));
  check_equal(parsed, FMT_TYPE_STRV);
  check_true(fmt_type_t_from_string("FMT_TYPE_BOOL", &parsed));
  check_equal(parsed, FMT_TYPE_BOOL);
  check_false(fmt_type_t_from_string("missing", &parsed));
  check_equal(parsed, FMT_TYPE_BOOL);
  check_false(fmt_type_t_from_string(NULL, &parsed));
  check_false(fmt_type_t_from_string("int", NULL));
  check_null(fmt_type_t_to_string((fmt_type_t)99));
}
```

- [ ] **Step 2: Add the C++ RED metadata test**

Add this behavior to `utils/tests/test_fmt_cpp.cpp`:

```cpp
it("should expose the same fmt metadata in C++") {
  const cmeta_enum_desc *meta = fmt_type_t_meta();
  fmt_type_t parsed = FMT_TYPE_NONE;

  check_not_null(meta);
  check_equal(meta->count, static_cast<size_t>(15));
  check_equal(fmt_type_t_to_symbol(FMT_TYPE_DOUBLE), "FMT_TYPE_DOUBLE");
  check_true(fmt_type_t_from_string("time", &parsed));
  check_equal(parsed, FMT_TYPE_TIME);
}
```

- [ ] **Step 3: Run RED and verify missing APIs are the only failure**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_fmt test_fmt_cpp"
```

Expected: compile failures naming `fmt_type_t_meta`, `fmt_type_t_to_string`, `fmt_type_t_to_symbol`, and `fmt_type_t_from_string`. ABI static assertions must compile before the API failures.

- [ ] **Step 4: Extend the fmt storage Schema and generate metadata**

In `utils/include/fmt.h`, include `<cmeta/enum.h>` instead of including only `<cmeta/pp.h>`.

Change every `FMT_DETAIL_TYPE_SCHEMA` row to this six-column form, preserving the explicit values, storage types, members, and constructors:

```c
#define FMT_DETAIL_TYPE_SCHEMA(M) \
  Schema(M, \
         (FMT_TYPE_CHAR, 1, "char", char, c, fmt_arg_char), \
         (FMT_TYPE_INT, 2, "int", int, i, fmt_arg_int), \
         (FMT_TYPE_UINT, 3, "uint", unsigned int, u, fmt_arg_uint), \
         (FMT_TYPE_LONG, 4, "long", long, l, fmt_arg_long), \
         (FMT_TYPE_ULONG, 5, "ulong", unsigned long, ul, fmt_arg_ulong), \
         (FMT_TYPE_LLONG, 6, "llong", long long, ll, fmt_arg_llong), \
         (FMT_TYPE_ULLONG, 7, "ullong", unsigned long long, ull, fmt_arg_ullong), \
         (FMT_TYPE_DOUBLE, 8, "double", double, f, fmt_arg_double), \
         (FMT_TYPE_STR, 9, "str", const char *, s, fmt_arg_str), \
         (FMT_TYPE_PTR, 10, "ptr", const void *, p, fmt_arg_ptr), \
         (FMT_TYPE_SIZE, 11, "size", size_t, sz, fmt_arg_size), \
         (FMT_TYPE_BOOL, 12, "bool", int, b, fmt_arg_bool), \
         (FMT_TYPE_STRV, 13, "strv", vstr, sv, fmt_arg_strv), \
         (FMT_TYPE_TIME, 14, "time", turbo_timeval_t, tv, fmt_arg_timeval))
```

Update `FMT_TYPE_ITEM`, `FMT_TYPE_FIELD`, and `FMT_MAKE_FN` to accept the added `text` parameter without changing generated declarations.

Before `#undef FMT_DETAIL_TYPE_SCHEMA`, generate the descriptor and helpers:

```c
#define FMT_TYPE_META_ITEM(name, value, text, type, member, constructor) \
  {(int64_t)(name), #name, (text)},

CMETA_LOCAL const cmeta_enum_item_desc fmt_type_t__enum_items[] = {
  {(int64_t)FMT_TYPE_NONE, "FMT_TYPE_NONE", "none"},
  Replay(FMT_DETAIL_TYPE_SCHEMA, FMT_TYPE_META_ITEM)
};

CMETA_LOCAL const cmeta_enum_desc fmt_type_t__enum_meta = {
  "fmt_type_t",
  fmt_type_t__enum_items,
  sizeof(fmt_type_t__enum_items) / sizeof(fmt_type_t__enum_items[0])
};

CMETA_INLINE const cmeta_enum_desc *fmt_type_t_meta(void) {
  return &fmt_type_t__enum_meta;
}

CMETA_INLINE const char *fmt_type_t_to_string(fmt_type_t value) {
  return cmeta_enum_to_string(&fmt_type_t__enum_meta, (int64_t)value);
}

CMETA_INLINE const char *fmt_type_t_to_symbol(fmt_type_t value) {
  return cmeta_enum_to_symbol(&fmt_type_t__enum_meta, (int64_t)value);
}

CMETA_INLINE bool fmt_type_t_from_string(const char *text, fmt_type_t *out) {
  int64_t raw;
  if (!out || !cmeta_enum_from_string(&fmt_type_t__enum_meta, text, &raw)) return false;
  *out = (fmt_type_t)raw;
  return true;
}

#undef FMT_TYPE_META_ITEM
```

Keep the numeric/object detection schemas separate and unchanged except for references to the same constructor names.

- [ ] **Step 5: Run GREEN fmt validation**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_fmt test_fmt_cpp && ctest --preset win-release-user -R ""^(test_fmt|test_fmt_cpp)$"" --output-on-failure"
clang -std=c11 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_fmt.c
clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_fmt_cpp.cpp
```

Expected: 2/2 fmt tests pass and both strict-warning compiles are clean.

- [ ] **Step 6: Check the Task 2 patch without committing overlapping user work**

```powershell
git diff --check -- utils/include/fmt.h utils/tests/test_fmt.c utils/tests/test_fmt_cpp.cpp
git diff HEAD -- utils/include/fmt.h utils/tests/test_fmt.c utils/tests/test_fmt_cpp.cpp
```

Expected: only the approved schema metadata, ABI mirror, and tests layered on the existing staged fmt work.

---

### Task 3: Reflected `turbo_log_entry_t` with unchanged ABI

**Files:**
- Modify: `utils/include/tlog.h`
- Modify: `utils/tests/test_tlog.c`
- Modify: `utils/tests/test_tlog_cpp.cpp`

**Interfaces:**
- Consumes: CMeta `Struct(...)`, `CMETA_ALIGNOF`, and the existing `turbo_log_level_t`.
- Produces: `turbo_log_entry_t_meta()` returning `const cmeta_struct_desc *`.

- [ ] **Step 1: Add the C RED metadata and ABI tests**

In `utils/tests/test_tlog.c`, after includes and before callback fixtures, add:

```c
typedef struct turbo_log_entry_legacy_layout {
  turbo_log_level_t level;
  uint64_t timestamp_ms;
  uint32_t thread_id;
  const char *component;
  const char *file;
  int line;
  const char *message;
  size_t message_len;
} turbo_log_entry_legacy_layout;

_Static_assert(sizeof(turbo_log_entry_t) == sizeof(turbo_log_entry_legacy_layout),
               "turbo_log_entry_t size changed");
_Static_assert(CMETA_ALIGNOF(turbo_log_entry_t) ==
                   CMETA_ALIGNOF(turbo_log_entry_legacy_layout),
               "turbo_log_entry_t alignment changed");
#define TLOG_ASSERT_ENTRY_OFFSET(field) \
  _Static_assert(offsetof(turbo_log_entry_t, field) == \
                     offsetof(turbo_log_entry_legacy_layout, field), \
                 "turbo_log_entry_t offset changed: " #field)
TLOG_ASSERT_ENTRY_OFFSET(level);
TLOG_ASSERT_ENTRY_OFFSET(timestamp_ms);
TLOG_ASSERT_ENTRY_OFFSET(thread_id);
TLOG_ASSERT_ENTRY_OFFSET(component);
TLOG_ASSERT_ENTRY_OFFSET(file);
TLOG_ASSERT_ENTRY_OFFSET(line);
TLOG_ASSERT_ENTRY_OFFSET(message);
TLOG_ASSERT_ENTRY_OFFSET(message_len);
#undef TLOG_ASSERT_ENTRY_OFFSET
```

Add this behavior after the existing log-level metadata test:

```c
it("should expose the stable log entry layout as read-only metadata") {
  const cmeta_struct_desc *meta = turbo_log_entry_t_meta();
  const char *names[] = {
      "level", "timestamp_ms", "thread_id", "component",
      "file", "line", "message", "message_len"};
  const char *types[] = {
      "turbo_log_level_t", "uint64_t", "uint32_t", "const char *",
      "const char *", "int", "const char *", "size_t"};
  const size_t offsets[] = {
      offsetof(turbo_log_entry_t, level),
      offsetof(turbo_log_entry_t, timestamp_ms),
      offsetof(turbo_log_entry_t, thread_id),
      offsetof(turbo_log_entry_t, component),
      offsetof(turbo_log_entry_t, file),
      offsetof(turbo_log_entry_t, line),
      offsetof(turbo_log_entry_t, message),
      offsetof(turbo_log_entry_t, message_len)};
  const size_t sizes[] = {
      sizeof(turbo_log_level_t), sizeof(uint64_t), sizeof(uint32_t),
      sizeof(const char *), sizeof(const char *), sizeof(int),
      sizeof(const char *), sizeof(size_t)};
  const size_t aligns[] = {
      CMETA_ALIGNOF(turbo_log_level_t), CMETA_ALIGNOF(uint64_t),
      CMETA_ALIGNOF(uint32_t), CMETA_ALIGNOF(const char *),
      CMETA_ALIGNOF(const char *), CMETA_ALIGNOF(int),
      CMETA_ALIGNOF(const char *), CMETA_ALIGNOF(size_t)};

  check_not_null(meta);
  check_equal(meta->name, "turbo_log_entry_t");
  check_equal(meta->size, sizeof(turbo_log_entry_t));
  check_equal(meta->align, CMETA_ALIGNOF(turbo_log_entry_t));
  check_equal(meta->field_count, (size_t)8);
  for (size_t i = 0; i < meta->field_count; ++i) {
    check_equal(meta->fields[i].name, names[i]);
    check_equal(meta->fields[i].type_name, types[i]);
    check_equal(meta->fields[i].offset, offsets[i]);
    check_equal(meta->fields[i].size, sizes[i]);
    check_equal(meta->fields[i].align, aligns[i]);
  }
  check_equal(cmeta_struct_find_field(meta, "component")->type_name,
              "const char *");
  check_equal(cmeta_struct_find_field(meta, "message")->size,
              sizeof(const char *));
  check_null(cmeta_struct_find_field(meta, "missing"));
}
```

- [ ] **Step 2: Add the C++ RED metadata test**

Add this behavior at the start of `spec("TLog C++ Tests")`:

```cpp
it("should expose log entry metadata to C++ consumers") {
  const cmeta_struct_desc *meta = turbo_log_entry_t_meta();

  check_not_null(meta);
  check_equal(meta->field_count, static_cast<size_t>(8));
  check_equal(meta->fields[0].offset, offsetof(turbo_log_entry_t, level));
  check_equal(meta->fields[7].offset, offsetof(turbo_log_entry_t, message_len));
}
```

- [ ] **Step 3: Run RED and verify missing metadata is the only failure**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tlog test_tlog_cpp"
```

Expected: compile failures naming `turbo_log_entry_t_meta`. ABI static assertions must compile before the missing-API errors.

- [ ] **Step 4: Declare the unchanged entry through CMeta Struct**

In `utils/include/tlog.h`, include `<cmeta/struct.h>` next to `<cmeta/enum.h>`. Replace only the explicit `turbo_log_entry_t` typedef with:

```c
/**
 * Log entry passed to sinks.
 *
 * component, file, and message are borrowed for the callback duration. The
 * reflected descriptor does not own or extend the lifetime of these strings.
 */
Struct(turbo_log_entry_t,
    (turbo_log_level_t, level),
    (uint64_t, timestamp_ms),
    (uint32_t, thread_id),
    (const char *, component),
    (const char *, file),
    (int, line),
    (const char *, message),
    (size_t, message_len)
);
```

Do not modify any callback typedef, entry initializer, async-copy code, or sink implementation.

- [ ] **Step 5: Run GREEN tlog validation**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tlog test_tlog_cpp && ctest --preset win-release-user -R ""^(test_tlog|test_tlog_cpp)$"" --output-on-failure"
clang -std=c11 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_tlog.c
clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_tlog_cpp.cpp
```

Expected: 2/2 tlog tests pass, C and C++ are warning-clean, and no source initializer needs modification.

- [ ] **Step 6: Check the Task 3 patch without committing overlapping user work**

```powershell
git diff --check -- utils/include/tlog.h utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp
git diff HEAD -- utils/include/tlog.h utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp
```

Expected: only the reflected declaration, ownership documentation, ABI mirror, and metadata tests layered on existing staged tlog work.

---

### Task 4: Integration, multi-TU, performance, and final verification

**Files:**
- Verify only: all files changed in Tasks 1–3

**Interfaces:**
- Consumes: all new CMeta/fmt/tlog metadata helpers.
- Produces: an evidence-backed compatibility and performance report; no new code unless verification reveals a scoped defect.

- [ ] **Step 1: Verify independent C/C++ consumers and public symbol semantics**

The four existing test translation units provide two independent C consumers (`test_fmt.c`, `test_tlog.c`) and two C++ consumers (`test_fmt_cpp.cpp`, `test_tlog_cpp.cpp`). Re-run strict syntax checks and scan for accidental exported implementations:

```powershell
clang -std=c11 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_fmt.c
clang -std=c11 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_tlog.c
clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_fmt_cpp.cpp
clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror -I cmeta/include -I utils/include -I utils/parser -I tinytest/include utils/tests/test_tlog_cpp.cpp
rg.exe -n "CXX_C_API.*(fmt_type_t_meta|turbo_log_entry_t_meta)|extern.*(fmt_type_t_meta|turbo_log_entry_t_meta)" cmeta utils
```

Expected: all four compiles succeed; `rg.exe` finds no exported/extern declaration for the TU-local helpers.

- [ ] **Step 2: Run the focused and adjacent regression matrix**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cmeta_core_test cmeta_header_cpp_test test_fmt test_fmt_cpp test_fmt_re2c test_tlog test_tlog_cpp test_turbo_str && ctest --preset win-release-user -R ""^(cmeta_core_test|cmeta_header_cpp_test|test_fmt|test_fmt_cpp|test_fmt_re2c|test_tlog|test_tlog_cpp|test_turbo_str)$"" --output-on-failure"
```

Expected: 8/8 tests pass.

- [ ] **Step 3: Measure compile time against Task 1 baselines**

Repeat the exact four-command five-sample script from Task 1. For each consumer compute:

```text
delta_percent = (new_median_ms - baseline_median_ms) / baseline_median_ms * 100
```

If any increase is over 10%, repeat nine samples, discard the first warm-up sample, and compare the median of the remaining eight. A confirmed regression over 10% stops completion and requires reducing header-generated metadata or obtaining explicit acceptance.

- [ ] **Step 4: Measure fmt/tlog runtime against Task 1 baselines**

Rebuild and run the same benchmark binaries three times without concurrent builds:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_fmt_bench bench_tlog"
1..3 | ForEach-Object { .\build\Msvc-Release\bin\test_fmt_bench.exe }
1..3 | ForEach-Object { .\build\Msvc-Release\bin\bench_tlog.exe }
```

Compare median `avg(us)` and `ops/s` for the six named rows from Task 1. A confirmed latency increase over 10% or throughput decrease over 10% stops completion.

- [ ] **Step 5: Compare Release sizes**

```powershell
Get-Item build/Msvc-Release/bin/turbo_utils.dll,
         build/Msvc-Release/bin/test_fmt.exe,
         build/Msvc-Release/bin/test_fmt_cpp.exe,
         build/Msvc-Release/bin/test_tlog.exe,
         build/Msvc-Release/bin/test_tlog_cpp.exe |
  Select-Object Name, Length
```

For each file compute `(new_bytes - baseline_bytes) / baseline_bytes * 100`. A size increase over 20% requires identifying the generated metadata responsible and either removing it from unused consumers or obtaining explicit acceptance.

- [ ] **Step 6: Run the fresh full Release build and CTest suite**

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure"
```

Expected: build exit code 0 and every discovered CTest passes.

- [ ] **Step 7: Final scope, residue, and dirty-worktree audit**

```powershell
codegraph sync .
codegraph affected -p . cmeta/include/cmeta/pp.h cmeta/include/cmeta/enum.h cmeta/include/cmeta/struct.h cmeta/include/cmeta/interface.h cmeta/include/cmeta/container.h utils/include/fmt.h utils/include/tlog.h
rg.exe -n "CMETA_(CONTAINER|IFACE)_(UNUSED|INLINE|LOCAL)" cmeta/include/cmeta
rg.exe -n "fmt_type_t_meta|turbo_log_entry_t_meta" cmeta utils
git diff --check -- cmeta/include/cmeta/pp.h cmeta/include/cmeta/enum.h cmeta/include/cmeta/struct.h cmeta/include/cmeta/interface.h cmeta/include/cmeta/container.h cmeta/tests/CMakeLists.txt cmeta/tests/cmeta_header_cpp_test.cpp utils/include/fmt.h utils/include/tlog.h utils/tests/test_fmt.c utils/tests/test_fmt_cpp.cpp utils/tests/test_tlog.c utils/tests/test_tlog_cpp.cpp
git status --short
```

Expected:

- no old module-local CMeta inline/unused macro definitions remain;
- metadata helper occurrences are limited to declarations/generation and tests;
- diff check is clean;
- no `fmt.c`, `tlog.c`, string, sink, queue, or runtime-registry file was changed by this plan;
- pre-existing staged and untracked files remain preserved.

Do not commit implementation paths. Report the exact test counts, compile-time deltas, benchmark deltas, size deltas, compatibility status, and remaining dirty-worktree state to the user for their integration decision.
