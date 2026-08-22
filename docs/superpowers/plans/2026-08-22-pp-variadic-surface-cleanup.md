# PP and Variadic Surface Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove duplicate business-layer arity/variadic engines, split raw versus formatted logging calls, and move TurboSTL-specific typed-container code generation out of CMeta while preserving runtime behavior and strict-C11 portability.

**Architecture:** CMeta remains the main repository's one finite PP kernel, while standalone TinyTest keeps its independent TinyMeta kernel. `fmt` and TLog consume those kernels without implementing empty-variadic tricks, and CMeta owns only the generic container protocol while TurboSTL owns concrete typed-facade generation.

**Tech Stack:** strict C11, C++17 compatibility tests, CMake presets, CTest, CMeta `Schema/Replay/CMETA_PP_FOR_EACH`, TinyMeta PP primitives, GCC/Clang/MSVC.

**Spec:** `docs/superpowers/specs/2026-08-22-pp-variadic-surface-cleanup-design.md`

## Global Constraints

- CMeta stays finite and strict-C11 based; do not add a generic empty-`__VA_ARGS__` detector.
- TinyTest stays independently usable and must not depend on CMeta.
- `fmt_print`, `turbo_log_str`, and `turbo_log_typed` runtime ABI signatures stay unchanged.
- Raw and formatted logging use distinct public macro names; do not preserve compatibility aliases that require `##__VA_ARGS__`.
- `fmt_text(buf, size, text)` is the canonical zero-format-argument formatting API; `fmt(buf, size, pattern, ...)` requires at least one formatting argument.
- CMeta owns container metadata/protocol only; TurboSTL owns raw-operation-specific wrapper generation.
- Do not redesign the formatting parser, TurboSTL algorithms, CFlow callable semantics, or Lean proofs.
- Standard verification uses `linux-release-user` and `win-release-user`; CI installs re2c as a host tool (apt on Linux, Chocolatey on Windows) and lets vcpkg manifest mode install package dependencies.

---

## File Structure

- `utils/include/fmt.h`: type-safe formatting type map and one non-empty argument expansion helper; no independent arity engine.
- `utils/include/tlog.h`: raw logging macros and formatted logging macros; no empty-varargs comma elision.
- `utils/tests/test_fmt.c`, `utils/tests/test_fmt_cpp.cpp`: explicit raw/formatted formatting contract.
- `utils/tests/test_tlog.c`, `utils/tests/test_tlog_cpp.cpp`, `utils/tests/test_tlog_header_collision.{c,cpp}`: logging macro contract and header collision coverage.
- `tinytest/include/tinymeta/pp.h`: TinyTest's sole PP count/repeat/select machinery.
- `tinytest/include/tinytest.h`: BDD/assertion semantics only; consumes TinyMeta PP helpers.
- `tinytest/include/tinymock.h`: continues to consume TinyMeta; zero-argument mocks remain explicit `TINYMOCk_MOCK0*` forms.
- `cmeta/include/cmeta/range.h`: `cmeta_range` traversal protocol only.
- `cmeta/include/cmeta/container.h`: small generic container descriptor/view protocol.
- `turbostl/include/turbostl/detail/typed_facade.h`: TurboSTL-owned typed wrapper/range/collector generation.
- `turbostl/include/turbostl/meta.h`: TurboSTL semantic kind rows and adapters consuming `detail/typed_facade.h`.
- `.github/workflows/cmeta.yml`: broadens the existing release-preset CI to cover this cross-module cleanup and run the relevant/full CTest suite.

---

### Task 1: Remove fmt's private arity engine and split zero-argument formatting

**Files:**
- Modify: `utils/include/fmt.h`
- Modify: `utils/tests/test_fmt.c`
- Modify: `utils/tests/test_fmt_cpp.cpp`
- Modify as discovered by the exact grep below: repository callers of three-argument `fmt(...)`

**Interfaces:**
- Consumes: `FMT_ARG(x)`, `CMETA_PP_FOR_EACH`, `CMETA_PP_NARG`, unchanged `fmt_print(char *, size_t, const char *, const fmt_arg_t *, size_t)`.
- Produces: `fmt_text(buf, size, text)` for zero format arguments; `fmt(buf, size, pattern, first_arg, ...)` for one or more format arguments; `FMT_ARGS(...)` remains only as a semantic non-empty argument-array helper.

- [ ] **Step 1: Add failing tests for the new explicit API split**

In `utils/tests/test_fmt.c`, add focused cases equivalent to:

```c
it("formats raw text without variadic arguments") {
  char buf[64];
  check_equal(fmt_text(buf, sizeof(buf), "ready"), 5);
  check_str_equal(buf, "ready");
}

it("formats one or more typed arguments") {
  char buf[64];
  check(fmt(buf, sizeof(buf), "{}:{}", 7, "ok") > 0);
  check_str_equal(buf, "7:ok");
}
```

Add the same public behavior to `utils/tests/test_fmt_cpp.cpp` using its existing TinyTest C++ assertion style.

- [ ] **Step 2: Run the fmt tests and verify RED**

Linux commands:

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user --target test_fmt test_fmt_cpp
ctest --preset linux-release-user -R '^test_fmt(_cpp)?$' --output-on-failure
```

Expected: compile failure because `fmt_text` does not exist yet.

- [ ] **Step 3: Replace `FMT_NARGS/FMT_WRAP_0..8` with the CMeta kernel**

Delete these definitions from `utils/include/fmt.h`:

```text
FMT_EXPAND
FMT_NARGS_IMPL
FMT_NARGS
FMT_WRAP_0 ... FMT_WRAP_8
FMT_WRAP_N_INNER
FMT_WRAP_N
```

Keep type detection and schema replay unchanged. Use one non-empty expansion helper:

```c
#define FMT_DETAIL_ARG_ITEM(arg, ignored) FMT_ARG(arg),
#define FMT_ARGS(...) \
  ((fmt_arg_t[]){ CMETA_PP_FOR_EACH(FMT_DETAIL_ARG_ITEM, ~, __VA_ARGS__) })
#define FMT_ARG_COUNT(...) CMETA_PP_NARG(__VA_ARGS__)
```

Define the public shapes without empty-varargs detection:

```c
#define fmt_text(buf, size, text) \
  fmt_print((buf), (size), (text), NULL, 0U)

#define fmt(buf, size, pattern, ...) \
  fmt_print((buf), (size), (pattern), FMT_ARGS(__VA_ARGS__), \
            (size_t)FMT_ARG_COUNT(__VA_ARGS__))
```

`fmt(...)` is documented and tested as requiring at least one formatting argument.

- [ ] **Step 4: Migrate zero-argument fmt call sites**

Run:

```bash
git grep -nE '\bfmt\([^,]+,[^,]+,[^,()]+\)' -- ':!vendor/**'
```

For each true three-argument formatting call, change only the function-like macro name from `fmt(` to `fmt_text(`. Do not change explicit `fmt_print(..., NULL, 0)` calls.

Then verify no deleted helper remains outside historical docs:

```bash
git grep -nE 'FMT_(NARGS|WRAP_[0-9]+|WRAP_N|EXPAND)' -- ':!docs/**' || true
```

Expected: no matches.

- [ ] **Step 5: Run fmt C and C++ tests GREEN**

```bash
cmake --build --preset linux-release-user --target test_fmt test_fmt_cpp test_fmt_re2c
ctest --preset linux-release-user -R '^test_fmt(_cpp|_re2c)?$' --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit Task 1**

```bash
git add utils/include/fmt.h utils/tests/test_fmt.c utils/tests/test_fmt_cpp.cpp
# add any migrated fmt callers reported by git status
git commit -m "refactor(fmt): remove private variadic arity engine"
```

---

### Task 2: Split TLog raw and formatted convenience surfaces

**Files:**
- Modify: `utils/include/tlog.h`
- Modify: `utils/tests/test_tlog.c`
- Modify: `utils/tests/test_tlog_cpp.cpp`
- Modify: `utils/tests/test_tlog_header_collision.c`
- Modify: `utils/tests/test_tlog_header_collision.cpp`
- Modify as discovered by the exact grep below: repository TLog call sites in `utils/`, examples, tests, benchmarks, and other modules.

**Interfaces:**
- Consumes: Task 1 `FMT_ARGS(...)`, `FMT_ARG_COUNT(...)`; unchanged `turbo_log_str` and `turbo_log_typed` runtime functions.
- Produces: raw `TLOG_DEBUG/INFO/WARN/ERROR/FATAL(message)` and `TURBO_LOG_*` raw forms; formatted `TLOG_DEBUGF/INFOF/WARNF/ERRORF/FATALF(pattern, first_arg, ...)` and `TURBO_LOG_*F` forms; generic raw `TURBO_LOG(logger, level, component, message)` plus formatted `TURBO_LOGF(...)`.

- [ ] **Step 1: Add failing raw/formatted logging tests**

Add cases that exercise both paths. The C test must include at least:

```c
TLOG_INFO("ready");
TLOG_INFOF("value={}", 7);
```

and explicit logger/component forms:

```c
TURBO_LOG_INFO(logger, "worker", "ready");
TURBO_LOG_INFOF(logger, "worker", "value={}", 7);
```

Keep the existing sink/capture assertions so the tests verify message text, level, component, and source location. Add equivalent C++ calls to `test_tlog_cpp.cpp`.

- [ ] **Step 2: Run TLog tests and verify RED**

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp test_tlog_header_collision_c test_tlog_header_collision_cpp
ctest --preset linux-release-user -R '^test_tlog' --output-on-failure
```

Expected: compile failure because the `*F` macros do not exist yet.

- [ ] **Step 3: Implement raw logging without variadic macros**

Introduce one internal raw helper that evaluates logger/message once:

```c
#define TURBO_LOG_RAW_IMPL(logger_expr, lvl, comp, message_expr) \
  do { \
    tlog_t *_log_ptr = (logger_expr); \
    if (_log_ptr && (lvl) >= tlog_get_level(_log_ptr)) { \
      const char *_log_message = (message_expr); \
      turbo_log_str(_log_ptr, (lvl), (comp), TURBO_LOG_SOURCE_FILE, \
                    TURBO_LOG_SOURCE_LINE, _log_message, \
                    _log_message ? strlen(_log_message) : 0U); \
    } \
  } while (0)
```

Define raw public macros with fixed arity only, for example:

```c
#define TLOG_INFO(message) \
  TURBO_LOG_RAW_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_INFO, NULL, (message))
#define TURBO_LOG_INFO(logger, component, message) \
  TURBO_LOG_RAW_IMPL((logger), TURBO_LOG_LEVEL_INFO, (component), (message))
```

Apply the same shape to DEBUG/WARN/ERROR/FATAL and the generic `TURBO_LOG`.

- [ ] **Step 4: Implement formatted logging as explicitly non-empty variadic**

C implementation:

```c
#define TURBO_LOG_FORMAT_IMPL(logger_expr, lvl, comp, pattern, ...) \
  do { \
    tlog_t *_log_ptr = (logger_expr); \
    if (_log_ptr && (lvl) >= tlog_get_level(_log_ptr)) { \
      turbo_log_typed(_log_ptr, (lvl), (comp), TURBO_LOG_SOURCE_FILE, \
                      TURBO_LOG_SOURCE_LINE, (pattern), FMT_ARGS(__VA_ARGS__), \
                      (size_t)FMT_ARG_COUNT(__VA_ARGS__)); \
    } \
  } while (0)
```

For C++, retain the existing native variadic-template helper but call it only from `*F` macros; remove `##__VA_ARGS__` from the macro call. Define `TLOG_INFOF`, `TURBO_LOG_INFOF`, and the DEBUG/WARN/ERROR/FATAL peers. Define generic `TURBO_LOGF` similarly.

- [ ] **Step 5: Migrate repository logging call sites**

Run:

```bash
git grep -nE '\b(TLOG|TURBO_LOG)_(DEBUG|INFO|WARN|ERROR|FATAL)\(' -- ':!docs/**' ':!vendor/**'
git grep -nE '\bTURBO_LOG\(' -- ':!docs/**' ':!vendor/**'
```

Migration rule is exact:

- Calls with only the fixed raw arguments keep their current macro name.
- Calls carrying formatting arguments after the pattern change to the corresponding `*F` macro.
- Do not convert calls whose message was already preformatted before the logging macro.

Then verify touched logging headers contain no comma-elision extension:

```bash
git grep -n '##__VA_ARGS__' -- utils/include/fmt.h utils/include/tlog.h
```

Expected: no matches.

- [ ] **Step 6: Run TLog and fmt tests GREEN**

```bash
cmake --build --preset linux-release-user --target test_fmt test_fmt_cpp test_tlog test_tlog_cpp test_tlog_header_collision_c test_tlog_header_collision_cpp
ctest --preset linux-release-user -R '^(test_fmt|test_tlog)' --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit Task 2**

```bash
git add utils/include/tlog.h utils/tests utils/examples utils/benchmarks
# add other migrated source files reported by git status
git commit -m "refactor(tlog): separate raw and formatted logging"
```

---

### Task 3: Make TinyMeta the only TinyTest PP kernel

**Files:**
- Modify: `tinytest/include/tinymeta/pp.h`
- Modify: `tinytest/include/tinytest.h`
- Modify only if needed for direct invocation sites: `tinytest/include/tinymock.h`
- Test: `tinytest/test/regression_fixes.c`
- Test: `tinytest/test/c11_generic_test.c`
- Test: `tinytest/test/tinymock_test.c`
- Test: `tinytest/test/regression_cpp.cpp`
- Test: `tinytest/test/tinymock_cpp_test.cpp`

**Interfaces:**
- Consumes: existing `TTEST_PP_NARG__`, `TTEST_PP_REPEAT__`, `TTEST_PP_ARG_AT__`, `TTEST_PP_COMMA_IF__`.
- Produces: `TTEST_PP_OVERLOAD__(prefix, ...)` (or equivalent single canonical selector in `tinymeta/pp.h`), explicit `TT_invoke0(func)` plus non-empty `TT_invoke(func, first_arg, ...)`; removes `TTEST_COUNT_ARGS__`, public-header-local overload expansion helpers, and empty-comma elision.

- [ ] **Step 1: Add regression coverage for one-vs-many node names and invoke arities**

In `tinytest/test/regression_fixes.c`, retain/add both forms:

```c
it("literal percent name") {
  check(true);
}

it("row %d", 7) {
  check(true);
}
```

Add one internal test fixture that exercises a zero-context-only invocation through `TT_invoke0(fn)` and a non-empty invocation through `TT_invoke(fn, value)` if those helpers are already test-visible; otherwise cover their public macro callers.

- [ ] **Step 2: Run TinyTest/TinyMock tests as baseline**

```bash
cmake --build --preset linux-release-user --target tinytest_regression_fixes c11_generic_test tinymock_test tinytest_regression_cpp tinymock_cpp_test
ctest --preset linux-release-user -R '^(tinytest_|c11_generic_test|tinymock_)' --output-on-failure
```

Expected before implementation: existing tests pass; the newly added explicit helper test fails to compile until the helper split exists.

- [ ] **Step 3: Centralize overload/count selection in `tinymeta/pp.h`**

Move generic overload concatenation into the kernel:

```c
#define TTEST_PP_OVERLOAD_I__(prefix, count) TTEST_PP_CAT__(prefix, count)
#define TTEST_PP_OVERLOAD__(prefix, ...) \
  TTEST_PP_OVERLOAD_I__(prefix, TTEST_PP_NARG__(__VA_ARGS__))
```

Where TinyTest needs one-argument literal versus two-or-more formatted node dispatch, implement that selector in `tinymeta/pp.h`, not `tinytest.h`. It must support 1..16 arguments and return one of two mapper names; no empty-list support is added.

- [ ] **Step 4: Remove TinyTest's second count/overload engine**

Delete from `tinytest.h`:

```text
TTEST_COUNT_ARGS__
TTEST_PATTERN_MATCH__
TTEST_OVERLOAD__
TTEST_EXPAND_OVERLOAD__
```

Change `TTEST_NODE__`, `TTEST_MACRO__`, and other non-empty dispatch sites to the TinyMeta kernel helper(s). Do not change assertion/test semantics.

- [ ] **Step 5: Remove empty-varargs `TT_invoke`**

Replace the current C23/GNU/MSVC comma-elision branch with explicit forms:

```c
#define TT_invoke0(func) func(ttest_active_config__)
#define TT_invoke(func, ...) func(ttest_active_config__, __VA_ARGS__)
```

Run:

```bash
git grep -n 'TT_invoke(' -- tinytest
```

Change every zero-extra-argument use to `TT_invoke0(...)`; leave non-empty uses on `TT_invoke(...)`. No `##__VA_ARGS__` or `__VA_OPT__` remains for this purpose.

- [ ] **Step 6: Verify TinyMock keeps explicit zero-argument forms**

Run:

```bash
git grep -nE 'TINYMOCk_MOCK0|TINYMOCk_MOCK0_VOID|TTEST_PP_(NARG|REPEAT|ARG_AT|COMMA_IF)' -- tinytest/include/tinymock.h
```

Expected: `TINYMOCk_MOCK0*` remain; non-zero mocks use TinyMeta primitives; no new mock-specific `NARGS/REPEAT_1..N` family is introduced.

- [ ] **Step 7: Run all TinyTest/TinyMock tests GREEN**

```bash
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^(tinytest_|c11_generic_test|tinymock_|math_test|test_tree_test|array_test|dynamic_test|before_after)$' --output-on-failure
```

Expected: selected TinyTest/TinyMock tests pass.

- [ ] **Step 8: Commit Task 3**

```bash
git add tinytest/include tinytest/test
git commit -m "refactor(tinytest): centralize preprocessor dispatch"
```

---

### Task 4: Move typed-container facade generation from CMeta to TurboSTL

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Rewrite smaller: `cmeta/include/cmeta/container.h`
- Modify: `cmeta/include/cmeta/meta.h`
- Create: `turbostl/include/turbostl/detail/typed_facade.h`
- Modify: `turbostl/include/turbostl/meta.h`
- Test: `cmeta/tests/cmeta_core_test.c`
- Test: `cmeta/tests/cmeta_language_surface_test.c`
- Test: `turbostl/tests/turbostl_typed_test.c`
- Test: `turbostl/tests/turbostl_header_typed_test.c`
- Test: `turbostl/tests/turbostl_header_typed_cpp_test.cpp`

**Interfaces:**
- Produces from CMeta: `cmeta_container_view`, `cmeta_container_desc`, `cmeta_container_header`, `cmeta_container_descriptor(const void *)`, `cmeta_container_range_view(const void *, cmeta_container_view, cmeta_range *)` in `cmeta/container.h`.
- Produces internally from TurboSTL: typed facade generation formerly named `CMETA_CONTAINER1_*`, `CMETA_CONTAINER2_*`, `CMETA_C1_INLINE_*`, `CMETA_C2_INLINE_*`, moved under `turbostl/detail/typed_facade.h` and renamed to a `TURBO_STL_TYPED_*` internal namespace.

- [ ] **Step 1: Add boundary tests before moving code**

In `cmeta/tests/cmeta_core_test.c`, include `<cmeta/container.h>` directly and construct a minimal descriptor/header pair that verifies:

```c
cmeta_container_header header = { &desc };
check(cmeta_container_descriptor(&header) == &desc);
```

Verify an invalid/missing view factory makes `cmeta_container_range_view` return false.

In `cmeta/tests/cmeta_language_surface_test.c`, add compile-time guards that TurboSTL facade macros are not part of the CMeta application surface after the move.

- [ ] **Step 2: Run CMeta/TurboSTL boundary tests and establish RED for the new ownership rule**

Before removing old generator names, add a temporary guard that fails if a representative `CMETA_CONTAINER1_DEFINE` remains visible from public CMeta includes. Run:

```bash
cmake --build --preset linux-release-user --target cmeta_core_test cmeta_language_surface_test turbostl_typed_test turbostl_header_typed_test turbostl_header_typed_cpp_test
ctest --preset linux-release-user -R '^(cmeta_|turbostl_(typed|header_typed))' --output-on-failure
```

Expected: boundary guard fails while the old CMeta generator remains.

- [ ] **Step 3: Split generic container protocol out of `range.h`**

Keep in `range.h` only `cmeta_range`, cursor, flags, range functions, and `cmeta_range_factory_fn`.

Move these definitions unchanged in behavior into the small `cmeta/container.h`:

```text
cmeta_container_view
cmeta_container_desc
cmeta_container_header
cmeta_container_descriptor
cmeta_container_range_view
```

`cmeta/container.h` includes `<cmeta/range.h>`; `range.h` must not include `container.h`. Add `<cmeta/container.h>` to the non-C++ CMeta aggregate where the protocol is intended to be available.

- [ ] **Step 4: Move facade generator code into TurboSTL detail**

Create `turbostl/include/turbostl/detail/typed_facade.h`. Move the raw-prefix/method-operation generator body from old `cmeta/container.h` into it.

Rename its internal namespace consistently:

```text
CMETA_CONTAINER1_DEFINE                  -> TURBO_STL_TYPED_CONTAINER1_DEFINE
CMETA_CONTAINER2_DEFINE                  -> TURBO_STL_TYPED_CONTAINER2_DEFINE
CMETA_CONTAINER1_INDEX_RANGE_DEFINE      -> TURBO_STL_TYPED_CONTAINER1_INDEX_RANGE_DEFINE
CMETA_CONTAINER1_LINK_RANGE_DEFINE       -> TURBO_STL_TYPED_CONTAINER1_LINK_RANGE_DEFINE
CMETA_C1_INLINE_*                        -> TURBO_STL_TYPED_C1_INLINE_*
CMETA_C2_INLINE_*                        -> TURBO_STL_TYPED_C2_INLINE_*
```

Apply the same `TURBO_STL_TYPED_` prefix rule to all moved facade-only helpers. Do not rename generic `cmeta_*` protocol types.

- [ ] **Step 5: Rewire TurboSTL semantic rows to the moved generator**

Change `turbostl/include/turbostl/meta.h` from:

```c
#include <cmeta/container.h>
```

to:

```c
#include <cmeta/container.h>
#include <turbostl/detail/typed_facade.h>
```

Update its adapters to call the renamed `TURBO_STL_TYPED_*` generator macros. Keep existing `TURBO_STL_KIND_ROW_*`, collector semantics, Range flags, and raw algorithm prefixes unchanged.

- [ ] **Step 6: Verify CMeta no longer knows TurboSTL operation names**

Run:

```bash
git grep -nE 'turbo_(vec|list|map|hash_map)|TURBO_META_|CMETA_C[12]_INLINE_|CMETA_CONTAINER[12]_DEFINE' -- cmeta/include cmeta/src
```

Expected: no facade-generation matches in CMeta. Generic `cmeta_container_*` protocol names are allowed.

- [ ] **Step 7: Run CMeta and full TurboSTL tests GREEN**

```bash
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^(cmeta_|turbostl_)' --output-on-failure
```

Expected: all CMeta and TurboSTL tests pass, including C11 and C++ typed header tests.

- [ ] **Step 8: Commit Task 4**

```bash
git add cmeta/include cmeta/tests turbostl/include turbostl/tests
git commit -m "refactor(turbostl): own typed facade generation"
```

---

### Task 5: Audit CFlow and eliminate pure PP forwarding aliases only

**Files:**
- Inspect/modify if the audit identifies pure PP renames: `cflow/include/cflow/meta.h`, `cflow/include/cflow/operators.h`
- Test: `cflow/tests/cflow_graph_test.c`
- Test: `cflow/tests/cflow_pipeline_test.c`
- Test: `cflow/tests/cflow_runtime_test.c`
- Test: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: CMeta `Replay`, `CMETA_PP_FOR_EACH_*`, typed/callable semantics.
- Produces: no new public API. CFlow semantic macros (`typed`, lambda/bind wrappers, operator schema normalization) remain when they encode CFlow behavior.

- [ ] **Step 1: Audit CFlow for a second generic arity engine**

Run:

```bash
git grep -nE '#define[[:space:]]+[A-Za-z0-9_]*(NARGS|COUNT_ARGS|WRAP_[0-9]+|APPLY_[0-9]+|REPEAT_[0-9]+)' -- cflow
```

Expected: no independent generic arity family. If the command is empty, make no CFlow code change. `Replay(...)`, `CMETA_PP_FOR_EACH_A(...)`, and semantic lambda/bind/typed macros are explicitly allowed.

- [ ] **Step 2: Remove only pure forwarding aliases found by direct inspection**

Inspect `cflow/include/cflow/meta.h` and `cflow/include/cflow/operators.h`. A macro may be removed only when its replacement is a direct CMeta PP primitive with identical arguments and the macro adds no operator/callable semantics. Do not change `CFLOW_WRAP_OP_TYPED`, `CFLOW_SIG_OF_OP`, `typed_raw`, `lambda*_raw`, `cmeta_bindable*`, or operator rows.

- [ ] **Step 3: Run all CFlow tests GREEN**

```bash
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^cflow_' --output-on-failure
```

Expected: all CFlow tests pass.

- [ ] **Step 4: Commit Task 5 only if files changed**

```bash
git status --short cflow
# If non-empty:
git add cflow
git commit -m "refactor(cflow): remove redundant PP forwarding"
```

If `git status --short cflow` is empty, record the audit result in the final Task 6 report instead of creating an empty commit.

---

### Task 6: Full repository macro audit, CI coverage, docs, and cross-platform verification

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Modify: `cmeta/LANGUAGE_REFERENCE.md` if container protocol wording needs the new ownership path
- Modify: `cmeta/README.md` and `turbostl/README.md` for the container boundary
- Modify: `utils/include/fmt.h`/`utils/include/tlog.h` comments to document raw vs formatted call shapes
- Create: `docs/superpowers/plans/2026-08-22-pp-variadic-surface-cleanup-audit.md`

**Interfaces:**
- Produces: final allowlist/audit evidence and release-preset CI evidence; no new runtime API.

- [ ] **Step 1: Run the full macro-surface audit**

Capture outputs for:

```bash
git grep -nE '#define[[:space:]]+[A-Za-z0-9_]*(NARGS|COUNT_ARGS|WRAP_[0-9]+|APPLY_[0-9]+|REPEAT_[0-9]+)' -- ':!vendor/**'
git grep -n '##__VA_ARGS__' -- ':!vendor/**'
git grep -n '__VA_OPT__' -- ':!vendor/**'
git grep -nE 'FMT_(NARGS|WRAP_[0-9]+|WRAP_N)|TTEST_COUNT_ARGS__|TTEST_PATTERN_MATCH__' -- ':!docs/**' || true
```

The only generic numbered iteration/count families allowed are inside:

```text
cmeta/include/cmeta/pp.h
tinytest/include/tinymeta/pp.h
```

Every remaining `##__VA_ARGS__` or `__VA_OPT__` occurrence outside those kernels must be listed in the audit document with its file, macro name, and semantic reason. The touched `fmt.h`, `tlog.h`, and TinyTest invocation surface must have none.

- [ ] **Step 2: Write the audit report with the actual grep results**

Create `docs/superpowers/plans/2026-08-22-pp-variadic-surface-cleanup-audit.md` with these fixed sections:

```markdown
# PP/Variadic Cleanup Audit

## Canonical kernels
- cmeta/include/cmeta/pp.h
- tinytest/include/tinymeta/pp.h

## Removed business-layer engines
- fmt: FMT_NARGS / FMT_WRAP_*
- tlog: empty-varargs raw/formatted dispatch
- TinyTest: TTEST_COUNT_ARGS__ / TTEST_PATTERN_MATCH__ / empty TT_invoke

## Remaining numbered PP families
<literal grep output or "none outside canonical kernels">

## Remaining comma-elision / __VA_OPT__ uses
<literal grep output plus one-line reason per retained occurrence, or "none">

## Container ownership
- CMeta: range/container protocol only
- TurboSTL: detail/typed_facade.h concrete generation

## CFlow audit
<literal result: no generic arity engine, or exact aliases removed>
```

Do not write prospective items; this file records actual post-change state.

- [ ] **Step 3: Broaden the existing CI trigger and test step**

Add `utils/**` to `.github/workflows/cmeta.yml` path filters because fmt/TLog are now part of the protected surface. Keep the existing release-user configure/build commands.

Change the test invocation from only `cmeta_|cflow_` to the complete configured CTest suite:

```bash
ctest --preset linux-release-user --output-on-failure
```

and on Windows:

```cmd
ctest --preset win-release-user --output-on-failure
```

Do not add a new CMake preset or a second workflow just for this cleanup.

- [ ] **Step 4: Run the complete Linux release verification locally/CI-equivalent**

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Expected: configure and build exit 0; CTest reports 0 failed tests.

- [ ] **Step 5: Run the complete Windows release verification through the existing CI path**

On a VS Developer Command Prompt / GitHub Windows runner with re2c available through Chocolatey:

```cmd
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

Expected: configure and build exit 0; CTest reports 0 failed tests.

- [ ] **Step 6: Final diff and compatibility audit**

Run:

```bash
git diff master...HEAD -- cmeta utils tinytest turbostl cflow .github/workflows/cmeta.yml
git grep -nE 'TLOG_(DEBUG|INFO|WARN|ERROR|FATAL)\([^\n]*,' -- ':!docs/**' || true
git grep -nE 'TLOG_(DEBUGF|INFOF|WARNF|ERRORF|FATALF)\(' -- ':!docs/**'
```

Inspect every remaining raw macro call with a comma to ensure the comma belongs to an expression rather than a forgotten formatting argument. Confirm no runtime ABI declaration changed for `fmt_print`, `turbo_log_str`, or `turbo_log_typed`.

- [ ] **Step 7: Commit Task 6**

```bash
git add .github/workflows/cmeta.yml cmeta/LANGUAGE_REFERENCE.md cmeta/README.md turbostl/README.md utils/include/fmt.h utils/include/tlog.h docs/superpowers/plans/2026-08-22-pp-variadic-surface-cleanup-audit.md
git commit -m "chore: enforce canonical PP surface"
```

- [ ] **Step 8: Open PR and require exact-head Linux/Windows GREEN before merge**

PR body must state:

```text
- fmt private NARGS/WRAP engine removed
- TLog raw and formatted APIs split; touched logging surface has no empty-varargs trick
- TinyTest uses TinyMeta as its sole PP kernel
- TurboSTL owns typed-facade generation; CMeta owns protocol only
- CFlow semantic DSL preserved
- full macro audit attached
- linux-release-user: configure/build/full CTest
- win-release-user: configure/build/full CTest
```

Do not merge if the PR head changes after the verified workflow run; rerun exact-head verification first.
