# PP and Variadic Surface Cleanup Design

## Purpose

The repository now has a capable strict-C11 preprocessor kernel in CMeta (`Schema`, `Replay`, `CMETA_PP_FOR_EACH`, `CMETA_PP_FOR_EACH_I`, `CMETA_PP_REPEAT`), while several older modules still carry independent arity-dispatch and variadic helper families. The result is duplicated macro machinery, non-standard empty-`__VA_ARGS__` handling, and blurred ownership between CMeta language mechanisms and framework-specific code generation.

This change removes those duplicate layers without turning CMeta into a universal preprocessor library. The rule is simple: each independently consumable product owns at most one small preprocessor kernel, and application/framework headers do not grow their own `NARGS`, `WRAP_1..N`, `APPLY_1..N`, or empty-variadic implementations.

## Scope

This design covers four related cleanup areas:

1. `utils/include/fmt.h` and logging convenience macros in `utils/include/tlog.h`.
2. TinyTest/TinyMock arity and invocation helpers.
3. TurboSTL typed-container code generation currently located in `cmeta/container.h`.
4. CFlow/CMeta forwarding aliases that duplicate existing PP primitives without adding semantics.

This design does not redesign formatting syntax, logging runtime behavior, TinyTest assertion semantics, TurboSTL container algorithms, CFlow operator semantics, or Lean proofs.

## Architectural Rule

### Main repository code

CMeta owns the shared strict-C11 PP kernel:

- `Schema(...)`
- `Replay(...)`
- `CMETA_PP_FOR_EACH(...)`
- `CMETA_PP_FOR_EACH_I(...)`
- `CMETA_PP_REPEAT(...)`
- minimal concatenation/unparenthesizing/count primitives required to implement those APIs

CMeta remains finite and deliberately bounded. New empty-variadic detection machinery is not added merely to preserve old convenience macro shapes.

### TinyTest

TinyTest remains an independent product and must not depend on CMeta. Therefore `tinytest/include/tinymeta/pp.h` remains as TinyTest's one minimal PP kernel. TinyTest and TinyMock must reuse it rather than maintaining additional count/dispatch families in public headers.

## Formatting API Cleanup

`fmt.h` currently already uses `Schema/Replay` correctly for type tables and `_Generic`/C++ overload generation. The obsolete layer is the second arity kernel:

- `FMT_EXPAND`
- `FMT_NARGS_IMPL`
- `FMT_NARGS`
- `FMT_WRAP_0` through `FMT_WRAP_8`
- `FMT_WRAP_N_INNER`
- `FMT_WRAP_N`

These are removed.

For non-empty formatted argument lists, the argument array is generated directly with CMeta foreach:

```c
#define FMT_DETAIL_ARG_ITEM(arg, ignored) FMT_ARG(arg),
```

and one shared helper builds the stack array. Argument count comes from the shared CMeta count primitive or from the generated array size where the call shape permits it. No `FMT_*` arity family remains.

### Zero-argument formatting

Strict C11 has no clean standard empty-`__VA_ARGS__` primitive. The implementation will not introduce complex empty-argument detection into CMeta.

Raw text and formatted text use explicit API shapes:

```c
fmt_text(buf, size, "ready");
fmt(buf, size, "value={}", value);
```

`fmt(...)` requires at least one formatting argument. `fmt_text(...)` is the canonical zero-format-argument API and routes to the existing formatter contract with `args == NULL` and `arg_count == 0`, unless a direct-copy implementation is proven behaviorally identical by the existing tests.

Low-level `fmt_print()` remains unchanged and continues to accept an explicit `fmt_arg_t *` plus count.

Existing in-repository zero-argument `fmt(...)` calls must be migrated to `fmt_text(...)`. No compatibility alias preserves the old zero-argument `fmt(...)` shape.

## Logging API Cleanup

`tlog.h` currently inherits `FMT_NARGS/FMT_ARGS` and uses `##__VA_ARGS__` to make the same macro accept both raw text and formatted calls. That coupling is removed.

The public convenience surface is split between raw-message and formatted-message forms:

```c
TLOG_INFO("ready");
TLOG_INFOF("value={}", value);

TURBO_LOG_INFO(logger, component, "ready");
TURBO_LOG_INFOF(logger, component, "value={}", value);
```

The same naming rule applies to DEBUG/WARN/ERROR/FATAL and the generic level-taking form where one exists: the existing name is the raw-message form and the `F` suffix is the formatted-message form.

The existing runtime entry points remain:

- `turbo_log_str(...)`
- `turbo_log_typed(...)`

Raw convenience macros call `turbo_log_str`. Formatted convenience macros call `turbo_log_typed` and require at least one formatting argument. Source capture (`__FILE__`, `__LINE__`) and level filtering remain at the call site. Existing default-logger acquisition behavior is preserved unless a separate bug is identified and approved outside this cleanup.

C++ may continue to use a variadic template helper internally for formatted logging because this is native C++ language functionality, not preprocessor arity emulation. The public C and C++ macro names follow the same raw-vs-formatted distinction.

All in-repository formatted calls using the old raw macro names are migrated to the corresponding `*F` macro. Compatibility aliases that require `##__VA_ARGS__` are not retained.

## TinyTest and TinyMock Cleanup

TinyTest's independent PP kernel remains. The cleanup target is duplication outside that kernel.

`tinytest.h` currently defines additional helpers such as `TTEST_COUNT_ARGS__`, `TTEST_OVERLOAD__`, and `TT_invoke` comma-elision logic. The target state is explicit:

- non-empty arity selection uses `TTEST_PP_NARG__` from `tinymeta/pp.h`;
- any reusable token concatenation/selection helper needed by TinyTest/TinyMock lives in `tinymeta/pp.h` rather than being reimplemented in `tinytest.h`;
- `TTEST_COUNT_ARGS__` is removed;
- generic zero-or-N invocation via `func(config, ##__VA_ARGS__)` is removed;
- zero-argument invocation uses an explicit zero-argument helper/form, while non-empty invocation requires at least one forwarded argument;
- no new `##__VA_ARGS__` dependency is introduced.

TinyMock already uses `TTEST_PP_REPEAT__`, `TTEST_PP_NARG__`, `TTEST_PP_ARG_AT__`, and `TTEST_PP_COMMA_IF__`. Those patterns remain canonical. Existing explicit zero-argument mock forms (`TINYMOCk_MOCK0`, `TINYMOCk_MOCK0_VOID`) remain and are preferred over empty-variadic detection.

TinyTest stays strict C11 and remains independently usable without CMeta.

## CMeta / TurboSTL Container Boundary

`cmeta/container.h` currently mixes two concepts:

1. generic container protocol metadata; and
2. TurboSTL-specific typed facade generation that knows raw function prefixes, operation names, wrapper method families, Range adapters, and collector factories.

Only the first belongs in CMeta.

The target ownership is:

```text
cmeta/range.h
  cmeta_range and traversal protocol

cmeta/container.h
  cmeta_container_desc
  cmeta_container_header
  cmeta_container_view
  descriptor/view helpers

turbostl/include/turbostl/detail/typed_facade.h
  TurboSTL-specific typed wrapper generation
  operation mapper families
  generated Range adapters
  generated collector bridges
```

`cmeta/container.h` becomes a small protocol header. The current large facade generator is moved under TurboSTL and renamed with TurboSTL-owned internal prefixes where appropriate. `turbostl/meta.h` consumes that internal generator.

`cmeta/range.h` then depends on the small container protocol only where the public Range/container-view API requires it; the two headers must not form an include cycle. The implementation may place shared forward declarations in the smaller dependency direction, but no new umbrella header is introduced for this purpose.

No TurboSTL raw container implementation moves into CMeta, and CMeta must not know concrete prefixes such as `turbo_vec`, `turbo_map`, or operation names such as `push`, `at`, or `put`.

## CFlow Cleanup

CFlow already relies on `Replay(...)` and CMeta foreach for operator/signature schemas. This is the intended direction.

The cleanup may remove forwarding aliases that merely rename existing CMeta PP primitives, but it must not redesign callable/lambda/bind semantics. CFlow-specific macros remain when they encode CFlow semantics rather than generic iteration mechanics.

## Other X-Macro Users

This change does not automatically replace every X-macro list in `utils/` with CMeta DSL syntax. For example, error-code tables and old enum utilities can be migrated separately when their public API is addressed.

The criterion for this cleanup is narrower: remove duplicated *preprocessor machinery*, not every data table expressed with macros.

## Compatibility Policy

This cleanup intentionally prefers one canonical surface over compatibility aliases.

Removed legacy implementation helpers are not preserved as deprecated aliases when doing so would keep duplicate PP logic alive. In particular:

- old `FMT_NARGS/FMT_WRAP_*` helpers are removed;
- positional/arity dispatch helpers in business headers are removed when the shared kernel can express the same operation;
- logging raw and formatted forms use distinct public macro names rather than empty-variadic tricks;
- `cmeta/container.h` no longer exports TurboSTL facade-generation internals.

Source compatibility may break for old zero-argument `fmt(...)` and old formatted `TLOG_*`/`TURBO_LOG_*` call shapes; all repository call sites are migrated in the same change. Runtime ABI functions remain stable. Any proposed runtime ABI change requires a separate explicit decision and is not part of this cleanup.

## Testing and Verification

The implementation is complete only when all of the following hold:

1. Repository search shows no surviving business-layer `NARGS`, `WRAP_1..N`, `APPLY_1..N`, or equivalent arity families except the canonical kernels in `cmeta/pp.h` and `tinytest/include/tinymeta/pp.h`.
2. Repository search shows no `##__VA_ARGS__` in the touched `fmt.h`, `tlog.h`, `tinytest.h`, TinyMock/TinyMeta surface, CFlow surface, or TurboSTL typed-facade surface. Any untouched occurrence elsewhere is inventoried rather than silently ignored.
3. `fmt` tests cover `fmt_text(...)` and non-empty `fmt(...)` separately, including C11 type detection and C++ overload behavior.
4. TLog tests cover raw and formatted macros, source capture, level filtering, default logger use, and C++ formatted helper behavior.
5. TinyTest/TinyMock C and C++ tests pass without CMeta dependency and exercise explicit zero-argument and non-empty invocation paths.
6. TurboSTL typed facade tests pass after moving generator ownership out of CMeta.
7. CMeta language-surface tests still pass, `cmeta/container.h` exposes only protocol concepts, and CMeta no longer exports TurboSTL-specific facade-generation macros.
8. CFlow tests pass without callable/lambda/bind semantic changes.
9. Standard project CI uses the existing `linux-release-user` and `win-release-user` configure/build/test presets.
10. A final repository-wide macro audit is included in the PR description, listing any remaining arity or comma-elision machinery and why it is intentionally retained.

## Non-Goals

- No new Lean proof work.
- No C23 requirement.
- No generic empty-`__VA_ARGS__` detector added to CMeta.
- No redesign of the formatting parser or re2c grammar.
- No rewrite of TurboSTL container algorithms.
- No conversion of every utility X-macro table in this pass.
- No attempt to make CMeta a universal macro metaprogramming language.

## Success Criterion

After this cleanup, a developer should be able to answer "where does macro iteration live?" with only two answers: CMeta for the main repository, and TinyMeta for standalone TinyTest. Formatting, logging, CFlow, and TurboSTL may define semantic schemas and mappers, but they no longer own separate arity engines. CMeta exposes reusable protocol concepts; framework-specific code generation stays with the framework that owns the concrete operations.
