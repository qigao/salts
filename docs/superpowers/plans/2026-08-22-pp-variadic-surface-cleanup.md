# PP and Variadic Surface Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove duplicate business-layer arity/variadic engines, split raw versus formatted logging calls, and move TurboSTL-specific typed-container code generation out of CMeta while preserving runtime behavior and strict-C11 portability.

**Architecture:** CMeta remains the main repository's one finite PP kernel, while standalone TinyTest keeps its independent TinyMeta kernel. `fmt` and TLog consume those kernels without empty-variadic tricks. CMeta owns the generic container protocol; TurboSTL owns concrete typed-facade generation.

**Tech Stack:** strict C11, C++17 compatibility tests, CMake presets, CTest, CMeta `Schema/Replay/CMETA_PP_FOR_EACH`, TinyMeta PP primitives, GCC/Clang/MSVC.

**Spec:** `docs/superpowers/specs/2026-08-22-pp-variadic-surface-cleanup-design.md`

## Global Constraints

- Do not add a generic empty-`__VA_ARGS__` detector to CMeta or TinyMeta.
- TinyTest remains independently usable and must not depend on CMeta.
- `fmt_print`, `turbo_log_str`, and `turbo_log_typed` signatures do not change.
- `fmt_text(buf, size, text)` is the zero-format-argument API; `fmt(buf, size, pattern, ...)` requires at least one formatting argument.
- Raw TLog macros keep the existing non-`F` names; formatted calls use new `*F` names.
- Compatibility aliases that preserve `##__VA_ARGS__` are not allowed.
- CMeta owns only range/container/collector protocol concepts; TurboSTL owns raw-prefix and operation-specific wrapper generation.
- Do not redesign the format parser, TurboSTL algorithms, CFlow callable semantics, or Lean proofs.
- Verification uses the existing `linux-release-user` and `win-release-user` presets.

---

### Task 1: Remove fmt's private arity engine

**Files:**
- Modify: `utils/include/fmt.h`
- Modify: `utils/tests/test_fmt.c`
- Modify: `utils/tests/test_fmt_cpp.cpp`
- Migrate repository three-argument `fmt(...)` callers found by the Step 4 grep.

**Interfaces:**
- Consumes: `FMT_ARG(x)`, `CMETA_PP_FOR_EACH`, `CMETA_PP_NARG`, unchanged `fmt_print(...)`.
- Produces: `fmt_text(buf,size,text)`, non-empty `FMT_ARGS(...)`, `FMT_ARG_COUNT(...)`, and `fmt(buf,size,pattern,...)` requiring at least one format argument.

- [ ] **Step 1: Add RED tests**

Add these public behaviors to `test_fmt.c` and equivalent C++ assertions to `test_fmt_cpp.cpp`:

```c
char buf[64];
check_equal(fmt_text(buf, sizeof(buf), "ready"), 5);
check_equal(buf, "ready");
check(fmt(buf, sizeof(buf), "{}:{}", 7, "ok") > 0);
check_equal(buf, "7:ok");
```

- [ ] **Step 2: Verify RED**

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user --target test_fmt test_fmt_cpp
```

Expected: compile failure because `fmt_text` is not defined.

- [ ] **Step 3: Delete the fmt arity family and use CMeta PP**

Delete:

```text
FMT_EXPAND
FMT_NARGS_IMPL
FMT_NARGS
FMT_WRAP_0 ... FMT_WRAP_8
FMT_WRAP_N_INNER
FMT_WRAP_N
```

Replace with:

```c
#define FMT_DETAIL_ARG_ITEM(arg, ignored) FMT_ARG(arg),
#define FMT_ARGS(...) \
  ((fmt_arg_t[]){ CMETA_PP_FOR_EACH(FMT_DETAIL_ARG_ITEM, ~, __VA_ARGS__) })
#define FMT_ARG_COUNT(...) CMETA_PP_NARG(__VA_ARGS__)

#define fmt_text(buf, size, text) \
  fmt_print((buf), (size), (text), NULL, 0U)

#define fmt(buf, size, pattern, ...) \
  fmt_print((buf), (size), (pattern), FMT_ARGS(__VA_ARGS__), \
            (size_t)FMT_ARG_COUNT(__VA_ARGS__))
```

Do not add an empty-list branch to `CMETA_PP_NARG`.

- [ ] **Step 4: Migrate zero-format-argument callers**

```bash
git grep -nE '\bfmt\([^,]+,[^,]+,[^,()]+\)' -- ':!vendor/**'
```

For each true three-argument `fmt(buf,size,text)` call, rename it to `fmt_text(buf,size,text)`. Leave explicit `fmt_print(..., NULL, 0)` unchanged.

Verify deletion:

```bash
git grep -nE 'FMT_(NARGS|WRAP_[0-9]+|WRAP_N|EXPAND)' -- ':!docs/**' || true
```

Expected: no matches.

- [ ] **Step 5: Verify GREEN and commit**

```bash
cmake --build --preset linux-release-user --target test_fmt test_fmt_cpp test_fmt_re2c
ctest --preset linux-release-user -R '^test_fmt(_cpp|_re2c)?$' --output-on-failure
git add -u
git commit -m "refactor(fmt): remove private variadic arity engine"
```

Expected: selected fmt tests pass.

---

### Task 2: Split TLog raw and formatted macros

**Files:**
- Modify: `utils/include/tlog.h`
- Modify: `utils/tests/test_tlog.c`
- Modify: `utils/tests/test_tlog_cpp.cpp`
- Modify: `utils/tests/test_tlog_header_collision.c`
- Modify: `utils/tests/test_tlog_header_collision.cpp`
- Migrate all repository TLog callers found by Step 5.

**Interfaces:**
- Consumes: Task 1 `FMT_ARGS`/`FMT_ARG_COUNT`; unchanged `turbo_log_str` and `turbo_log_typed`.
- Produces: raw `TLOG_DEBUG/INFO/WARN/ERROR/FATAL(message)`, raw `TURBO_LOG_*`, formatted `TLOG_*F`, formatted `TURBO_LOG_*F`, raw generic `TURBO_LOG`, formatted generic `TURBO_LOGF`.

- [ ] **Step 1: Add RED tests for both call shapes**

C and C++ tests must exercise:

```c
TLOG_INFO("ready");
TLOG_INFOF("value={}", 7);
TURBO_LOG_INFO(logger, "worker", "ready");
TURBO_LOG_INFOF(logger, "worker", "value={}", 7);
```

Reuse existing capture sinks to verify message, level, component, file, and line.

- [ ] **Step 2: Verify RED**

```bash
cmake --build --preset linux-release-user --target test_tlog test_tlog_cpp test_tlog_header_collision_c test_tlog_header_collision_cpp
```

Expected: compile failure because `*F` macros are not defined.

- [ ] **Step 3: Implement fixed-arity raw logging**

Add one internal helper:

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

Define non-variadic DEBUG/INFO/WARN/ERROR/FATAL wrappers and `TURBO_LOG(logger,level,component,message)` on top of it.

- [ ] **Step 4: Implement non-empty formatted logging**

For C:

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

Define `TLOG_DEBUGF/INFOF/WARNF/ERRORF/FATALF`, `TURBO_LOG_DEBUGF/...`, and `TURBO_LOGF`. Each formatted macro requires at least one argument after `pattern`.

For C++, keep the existing variadic-template helper, but call it only from the `*F` macros with plain `__VA_ARGS__`; delete `##__VA_ARGS__`.

- [ ] **Step 5: Migrate callers**

```bash
git grep -nE '\b(TLOG|TURBO_LOG)_(DEBUG|INFO|WARN|ERROR|FATAL)\(' -- ':!docs/**' ':!vendor/**'
git grep -nE '\bTURBO_LOG\(' -- ':!docs/**' ':!vendor/**'
```

Rule: calls with only fixed raw parameters keep their name; calls with format arguments after the pattern change to the corresponding `*F` name.

Verify:

```bash
git grep -n '##__VA_ARGS__' -- utils/include/fmt.h utils/include/tlog.h
```

Expected: no matches.

- [ ] **Step 6: Verify GREEN and commit**

```bash
cmake --build --preset linux-release-user --target test_fmt test_fmt_cpp test_tlog test_tlog_cpp test_tlog_header_collision_c test_tlog_header_collision_cpp
ctest --preset linux-release-user -R '^(test_fmt|test_tlog)' --output-on-failure
git add -u
git commit -m "refactor(tlog): separate raw and formatted logging"
```

---

### Task 3: Make TinyMeta the only TinyTest PP kernel

**Files:**
- Modify: `tinytest/include/tinymeta/pp.h`
- Modify: `tinytest/include/tinytest.h`
- Modify invocation sites reported by `git grep -n 'TT_invoke(' -- tinytest`.
- Test: `tinytest/test/regression_fixes.c`
- Test: `tinytest/test/c11_generic_test.c`
- Test: `tinytest/test/tinymock_test.c`
- Test: `tinytest/test/regression_cpp.cpp`
- Test: `tinytest/test/tinymock_cpp_test.cpp`

**Interfaces:**
- Consumes: `TTEST_PP_NARG__`, `TTEST_PP_REPEAT__`, `TTEST_PP_ARG_AT__`, `TTEST_PP_COMMA_IF__`.
- Produces: exact `TTEST_PP_OVERLOAD__`, exact `TTEST_PP_ONE_OR_MANY__`, explicit `TT_invoke0`, and non-empty `TT_invoke`.

- [ ] **Step 1: Add RED regression coverage**

Keep/add both node forms in `regression_fixes.c`:

```c
it("literal percent name") { check(true); }
it("row %d", 7) { check(true); }
```

Add these internal callbacks and test body to the same translation unit:

```c
static int tt_invoke_probe__ = 0;
static void tt_invoke_zero__(ttest_config_type__ *config) {
  (void)config;
  tt_invoke_probe__ += 1;
}
static void tt_invoke_one__(ttest_config_type__ *config, int value) {
  (void)config;
  tt_invoke_probe__ += value;
}

it("invokes context helpers without empty variadics") {
  tt_invoke_probe__ = 0;
  TT_invoke0(tt_invoke_zero__);
  TT_invoke(tt_invoke_one__, 7);
  check_equal(tt_invoke_probe__, 8);
}
```

Expected before implementation: compile failure because `TT_invoke0` is not defined.

- [ ] **Step 2: Add canonical selectors to TinyMeta**

Add to `tinymeta/pp.h`:

```c
#define TTEST_PP_OVERLOAD_I__(prefix, count) TTEST_PP_CAT__(prefix, count)
#define TTEST_PP_OVERLOAD__(prefix, ...) \
  TTEST_PP_OVERLOAD_I__(prefix, TTEST_PP_NARG__(__VA_ARGS__))

#define TTEST_PP_ONE_OR_MANY_1__(one, many) one
#define TTEST_PP_ONE_OR_MANY_2__(one, many) many
#define TTEST_PP_ONE_OR_MANY_3__(one, many) many
#define TTEST_PP_ONE_OR_MANY_4__(one, many) many
#define TTEST_PP_ONE_OR_MANY_5__(one, many) many
#define TTEST_PP_ONE_OR_MANY_6__(one, many) many
#define TTEST_PP_ONE_OR_MANY_7__(one, many) many
#define TTEST_PP_ONE_OR_MANY_8__(one, many) many
#define TTEST_PP_ONE_OR_MANY_9__(one, many) many
#define TTEST_PP_ONE_OR_MANY_10__(one, many) many
#define TTEST_PP_ONE_OR_MANY_11__(one, many) many
#define TTEST_PP_ONE_OR_MANY_12__(one, many) many
#define TTEST_PP_ONE_OR_MANY_13__(one, many) many
#define TTEST_PP_ONE_OR_MANY_14__(one, many) many
#define TTEST_PP_ONE_OR_MANY_15__(one, many) many
#define TTEST_PP_ONE_OR_MANY_16__(one, many) many
#define TTEST_PP_ONE_OR_MANY__(one, many, ...) \
  TTEST_PP_INDEXED_NAME__(TTEST_PP_ONE_OR_MANY_, TTEST_PP_NARG__(__VA_ARGS__))(one, many)
```

This numbered selector is allowed because it lives in TinyTest's sole PP kernel.

- [ ] **Step 3: Delete TinyTest's second count/overload engine**

Delete from `tinytest.h`:

```text
TTEST_COUNT_ARGS__
TTEST_PATTERN_MATCH__
TTEST_OVERLOAD__
TTEST_EXPAND_OVERLOAD__
```

Replace:

```c
#define TTEST_MACRO__(M, ...) TTEST_PP_OVERLOAD__(M, __VA_ARGS__)(__VA_ARGS__)

#define TTEST_NODE__(flags, node_list, type, ...) \
  TTEST_PP_ONE_OR_MANY__(TTEST_NODE_DISPATCH_ONE__, TTEST_NODE_DISPATCH__, __VA_ARGS__)( \
      flags, node_list, type, __VA_ARGS__)
```

Change other numeric suffix dispatch sites to `TTEST_PP_OVERLOAD__` without changing their semantics.

- [ ] **Step 4: Delete empty-varargs invocation logic**

Replace the C23/GNU/MSVC branch with:

```c
#define TT_invoke0(func) func(ttest_active_config__)
#define TT_invoke(func, ...) func(ttest_active_config__, __VA_ARGS__)
```

Run:

```bash
git grep -n 'TT_invoke(' -- tinytest
```

Change every call with no argument after `func` to `TT_invoke0(func)`. Non-empty uses remain `TT_invoke(func, ...)`.

- [ ] **Step 5: Verify TinyMock uses TinyMeta and explicit zero forms**

```bash
git grep -nE 'TINYMOCk_MOCK0|TINYMOCk_MOCK0_VOID|TTEST_PP_(NARG|REPEAT|ARG_AT|COMMA_IF)' -- tinytest/include/tinymock.h
git grep -nE 'TINYMOCk_(NARGS|REPEAT_[0-9]+|COUNT_ARGS)' -- tinytest/include/tinymock.h || true
```

Expected: explicit `MOCK0*` remain; second command has no matches.

- [ ] **Step 6: Verify GREEN and commit**

```bash
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^(tinytest_|c11_generic_test|tinymock_|math_test|test_tree_test|array_test|dynamic_test|before_after)$' --output-on-failure
git add tinytest/include tinytest/test
git commit -m "refactor(tinytest): centralize preprocessor dispatch"
```

---

### Task 4: Move typed-container facade generation from CMeta to TurboSTL

**Files:**
- Modify: `cmeta/include/cmeta/range.h`
- Rewrite: `cmeta/include/cmeta/container.h`
- Modify: `cmeta/include/cmeta/meta.h`
- Create: `turbostl/include/turbostl/detail/typed_facade.h`
- Modify: `turbostl/include/turbostl/meta.h`
- Modify tests: `cmeta/tests/cmeta_core_test.c`, `cmeta/tests/cmeta_language_surface_test.c`, `turbostl/tests/turbostl_typed_test.c`, `turbostl/tests/turbostl_header_typed_test.c`, `turbostl/tests/turbostl_header_typed_cpp_test.cpp`

**Interfaces:**
- CMeta produces: `cmeta_container_view`, `cmeta_container_desc`, `cmeta_container_header`, `cmeta_container_descriptor`, `cmeta_container_range_view` from `cmeta/container.h`.
- TurboSTL internally produces the old facade generator under a `TURBO_STL_TYPED_*` namespace.

- [ ] **Step 1: Add RED boundary coverage**

Add a direct `<cmeta/container.h>` protocol test that builds a minimal `cmeta_container_desc`/`cmeta_container_header` pair and verifies descriptor lookup plus a false result for an unavailable view.

Add to `cmeta_language_surface_test.c`:

```c
#ifdef CMETA_CONTAINER1_DEFINE
#error "TurboSTL typed facade generator leaked into CMeta"
#endif
```

Before the move this guard is expected to fail.

- [ ] **Step 2: Split protocol from Range**

Keep only `cmeta_range*`, cursor, flags, range helpers, and `cmeta_range_factory_fn` in `range.h`.

Move these unchanged in behavior to the small `container.h`:

```text
cmeta_container_view
cmeta_container_desc
cmeta_container_header
cmeta_container_descriptor
cmeta_container_range_view
```

`container.h` includes `range.h`; `range.h` does not include `container.h`. Add `container.h` to `cmeta/meta.h` where the aggregate exposes runtime protocols.

- [ ] **Step 3: Move and rename the concrete generator**

Create `turbostl/include/turbostl/detail/typed_facade.h` and move the raw-prefix/operation-specific generator body from old `cmeta/container.h` into it.

Apply these namespace rules mechanically:

```text
CMETA_CONTAINER1_DEFINE             -> TURBO_STL_TYPED_CONTAINER1_DEFINE
CMETA_CONTAINER2_DEFINE             -> TURBO_STL_TYPED_CONTAINER2_DEFINE
CMETA_CONTAINER1_INDEX_RANGE_DEFINE -> TURBO_STL_TYPED_CONTAINER1_INDEX_RANGE_DEFINE
CMETA_CONTAINER1_LINK_RANGE_DEFINE  -> TURBO_STL_TYPED_CONTAINER1_LINK_RANGE_DEFINE
CMETA_C1_INLINE_*                   -> TURBO_STL_TYPED_C1_INLINE_*
CMETA_C2_INLINE_*                   -> TURBO_STL_TYPED_C2_INLINE_*
```

Every other moved facade-only `CMETA_CONTAINER*` helper receives the same `TURBO_STL_TYPED_` ownership prefix. Generic `cmeta_*` protocol types are not renamed.

- [ ] **Step 4: Rewire TurboSTL**

`turbostl/include/turbostl/meta.h` must include:

```c
#include <cmeta/container.h>
#include <turbostl/detail/typed_facade.h>
```

Update its generator invocations to the new names. Do not change `TURBO_STL_KIND_ROW_*`, Range flags, collectors, or raw algorithm prefixes.

- [ ] **Step 5: Verify ownership boundary and GREEN**

```bash
git grep -nE 'turbo_(vec|list|map|hash_map)|TURBO_META_|CMETA_C[12]_INLINE_|CMETA_CONTAINER[12]_DEFINE' -- cmeta/include cmeta/src || true
cmake --build --preset linux-release-user
ctest --preset linux-release-user -R '^(cmeta_|turbostl_)' --output-on-failure
```

Expected: grep has no facade-generation match in CMeta; all CMeta/TurboSTL tests pass.

- [ ] **Step 6: Commit**

```bash
git add cmeta/include cmeta/tests turbostl/include turbostl/tests
git commit -m "refactor(turbostl): own typed facade generation"
```

---

### Task 5: CFlow audit plus repository-wide enforcement

**Files:**
- Inspect: `cflow/include/cflow/meta.h`, `cflow/include/cflow/operators.h`
- Modify only literal forwarding aliases that add no CFlow semantics.
- Modify: `.github/workflows/cmeta.yml`
- Modify docs: `cmeta/LANGUAGE_REFERENCE.md`, `cmeta/README.md`, `turbostl/README.md`, comments in `fmt.h`/`tlog.h`
- Create: `docs/superpowers/plans/2026-08-22-pp-variadic-surface-cleanup-audit.md`

**Interfaces:**
- No new runtime API. Final output is the canonical-kernel allowlist and exact-head cross-platform verification.

- [ ] **Step 1: Audit CFlow**

```bash
git grep -nE '#define[[:space:]]+[A-Za-z0-9_]*(NARGS|COUNT_ARGS|WRAP_[0-9]+|APPLY_[0-9]+|REPEAT_[0-9]+)' -- cflow || true
```

Expected: no independent generic arity family. `Replay`, `CMETA_PP_FOR_EACH_A`, `typed_raw`, `lambda*_raw`, `cmeta_bindable*`, and operator-row macros remain because they carry CFlow semantics.

A CFlow alias is removed only when its body is a direct call to one CMeta PP primitive with the same argument order and no additional token generation or semantic validation.

- [ ] **Step 2: Run the final macro audit**

```bash
git grep -nE '#define[[:space:]]+[A-Za-z0-9_]*(NARGS|COUNT_ARGS|WRAP_[0-9]+|APPLY_[0-9]+|REPEAT_[0-9]+)' -- ':!vendor/**'
git grep -n '##__VA_ARGS__' -- ':!vendor/**' || true
git grep -n '__VA_OPT__' -- ':!vendor/**' || true
git grep -nE 'FMT_(NARGS|WRAP_[0-9]+|WRAP_N)|TTEST_COUNT_ARGS__|TTEST_PATTERN_MATCH__' -- ':!docs/**' || true
```

Only `cmeta/include/cmeta/pp.h` and `tinytest/include/tinymeta/pp.h` may contain generic numbered iteration/count/select families. The final command must return no matches.

- [ ] **Step 3: Record literal audit results**

Create `docs/superpowers/plans/2026-08-22-pp-variadic-surface-cleanup-audit.md` with:

```markdown
# PP/Variadic Cleanup Audit
## Canonical kernels
- cmeta/include/cmeta/pp.h
- tinytest/include/tinymeta/pp.h
## Removed business-layer engines
## Remaining numbered PP families
## Remaining comma-elision / __VA_OPT__ uses
## Container ownership
## CFlow audit
```

Paste actual grep output under the corresponding headings. Every retained `##__VA_ARGS__`/`__VA_OPT__` occurrence requires a one-line semantic reason; otherwise write `none`.

- [ ] **Step 4: Broaden existing CI instead of creating a new one**

Add `utils/**` to `.github/workflows/cmeta.yml` path filters. Keep:

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user
```

and the corresponding Windows commands. Change CTest from the old CMeta/CFlow regex to the complete configured suite:

```bash
ctest --preset linux-release-user --output-on-failure
```

```cmd
ctest --preset win-release-user --output-on-failure
```

Keep apt/Chocolatey re2c setup and vcpkg manifest behavior already present in the workflow.

- [ ] **Step 5: Full Linux verification**

```bash
cmake --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Expected: configure/build exit 0 and CTest reports 0 failed tests.

- [ ] **Step 6: Full Windows verification**

From the existing Windows CI path/VS Developer Command Prompt:

```cmd
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

Expected: configure/build exit 0 and CTest reports 0 failed tests.

- [ ] **Step 7: Commit and open PR**

```bash
git add .github/workflows/cmeta.yml cflow cmeta/LANGUAGE_REFERENCE.md cmeta/README.md turbostl/README.md utils/include/fmt.h utils/include/tlog.h docs/superpowers/plans/2026-08-22-pp-variadic-surface-cleanup-audit.md
git commit -m "chore: enforce canonical PP surface"
```

PR description must state that fmt private arity helpers are removed, TLog raw/formatted APIs are split, TinyTest uses TinyMeta as its sole PP kernel, TurboSTL owns typed-facade generation, CFlow semantics are unchanged, and full Linux/Windows release-preset CTest passed. Merge only the exact PR head that produced both successful workflows.
