# TLog vstr ABI Migration Design

Date: 2026-09-13

## Goal

Make Salts `tlog` use the unified `tstr`/`vstr` string model introduced by #261 at its public logging boundary. This phase intentionally breaks the existing `tlog` public ABI and source API where string fields or parameters are semantically borrowed slices.

The target invariant is:

- borrowed input and log-record strings use `vstr`;
- owned mutable construction uses `tstr`;
- NUL-terminated `const char *` remains only where an external API or operating-system boundary genuinely requires it;
- no parallel legacy/fallback logging ABI is introduced.

## Scope

This migration covers:

1. `salts_log_entry_t` string fields;
2. raw and typed logging entry points;
3. async log-entry storage and queue reconstruction;
4. sink callbacks and decorators that consume `salts_log_entry_t`;
5. component filtering and pattern rendering;
6. convenience macros/adapters at the C-string edge;
7. regression tests for non-NUL-terminated component, file, message, and format-pattern views.

This migration does not redesign the asynchronous queue algorithm, memory pool, sink ownership model, logger lifecycle, rotation policy, or CMeta enum semantics.

## Public ABI

`salts_log_entry_t` changes from borrowed C strings plus a separate message length to explicit borrowed views:

```c
CMETA_STRUCT(salts_log_entry_t,
    (salts_log_level_t, level),
    (uint64_t, timestamp_ms),
    (uint32_t, thread_id),
    (vstr, component),
    (vstr, file),
    (int, line),
    (vstr, message)
);
```

`message_len` is removed. `entry.message.len` is the only message-length authority.

The callback lifetime contract remains unchanged in spirit: `component`, `file`, and `message` are borrowed and valid only for the duration documented by the producing path. Sink callbacks may inspect them during the callback and must copy if they need retention.

Empty/absent values use a valid empty view. Code must not rely on `NULL` versus empty-string pointer identity to distinguish absence. Presence-sensitive behavior such as source capture should use `view.len == 0` as the empty case unless a separate semantic flag is introduced in a future design.

## Logging Entry Points

The raw primitive becomes view-native:

```c
void salts_log_str(tlog_t *logger,
                   salts_log_level_t level,
                   vstr component,
                   vstr file,
                   int line,
                   vstr message);
```

The typed primitive becomes view-native for all borrowed string inputs:

```c
void salts_log_typed(tlog_t *logger,
                     salts_log_level_t level,
                     vstr component,
                     vstr file,
                     int line,
                     vstr pattern,
                     const fmt_arg_t *args,
                     size_t arg_count);
```

`salts_log_typed` must use the bounded formatter path from #261 (`fmt_print_v` / `fmt_print_tstr_v` as appropriate). It must not convert the pattern back to a C string or call `strlen` on it.

No `salts_log_str_v`, `salts_log_typed_v`, compatibility shim ABI, or fallback path is added. The old signatures are replaced.

## Convenience Macros

Convenience macros remain ergonomic but become adapters at the outermost edge.

For compile-time literals, use `VSTR_LIT(...)` where the macro can safely require a literal. For general C-string expressions, evaluate the expression once and adapt with `vstr_from_cstr(...)`.

The macro layer must preserve current single-evaluation behavior and level filtering. It must not duplicate formatting or async-copy logic.

Source capture uses a view form derived from `__FILE__` when enabled and an empty view when disabled.

## Async Ownership and Queue Storage

Caller-provided `vstr` values are borrowed only until the logging call returns. Asynchronous publication must copy component, file, and message bytes into queue-owned storage before returning.

The async record stores views, not C-string pointers plus ad-hoc lengths:

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

`async_entry_create` computes storage from the supplied view lengths directly. It copies exactly `len` bytes for each view. No `strlen`, `vstr_from_cstr`, or required terminator is involved.

Queue-owned copies may still include alignment/padding bytes for allocator or SIMD safety, but padding is not part of the logical view and consumers must use the stored lengths.

When the queue entry is delivered to sinks, the public `salts_log_entry_t` views point into that queue-owned storage and remain valid for the sink callback duration.

## Sink and Decorator Behavior

All sinks and decorators consume the `vstr` fields directly.

### Pattern rendering

`format_with_pattern` copies `component.data/component.len`, `file.data/file.len`, and `message.data/message.len` directly. Existing repeated `vstr_from_cstr` conversions disappear.

### Filter sink

Exact component matching becomes length-aware byte comparison. The filter configuration itself may remain C-string based if it is owned/canonicalized during sink construction, but the runtime entry comparison must not require the entry to be NUL terminated.

A later phase may migrate sink option configuration strings independently; this ABI migration is focused on borrowed runtime logging data.

### Format decorator

The decorator-created entry exposes its temporary formatted message as a `vstr` pointing to decorator-owned stack storage valid only during the inner sink callback. It does not synthesize a NUL requirement for consumers.

### Metrics sink

Byte accounting uses `entry.message.len`.

### Callback/custom sinks

Callbacks receive the new `salts_log_entry_t` ABI. This is an intentional source and binary compatibility break.

## Boundaries That Stay C Strings

This migration does not mechanically replace every `const char *` in `tlog`.

The following may remain C strings where NUL termination is semantically required or convenient at construction time:

- file-system paths passed to `salts_fs`/OS APIs;
- sink configuration pattern/path strings that are immediately copied/compiled into owned storage;
- environment/configuration names;
- APIs whose contract explicitly requires NUL termination.

The rule is semantic: runtime borrowed slices become `vstr`; external NUL-based boundaries remain C strings until a separate justified migration.

## Error and Empty-View Semantics

`vstr` arguments must satisfy the existing validity rule: `data == NULL` is valid only when `len == 0`.

Invalid views are rejected at the public logging boundary without dereferencing them. Because logging APIs return `void`, rejection behaves like other filtered/dropped log events and must not crash or read out of bounds.

An empty message is valid. An empty component or file is valid. Formatting must never read beyond a view length even if the backing bytes are not NUL terminated.

## CMeta and Reflection

The reflected `salts_log_entry_t` descriptor changes with the ABI. The old `message_len` field disappears and the three string fields are reflected as `vstr`.

CMeta conformance must continue to pass on all supported platforms. Any serializer/introspection assumptions that previously treated those fields as C strings must be updated to the `vstr` representation rather than preserving compatibility aliases.

## TDD Contract

Implementation starts with RED tests that prove the old ABI cannot satisfy the new contract.

Required RED cases:

1. **Raw non-NUL message** — publish a message backed by a byte array with no terminator and verify a callback receives exactly the intended bytes and length.
2. **Non-NUL component and file** — publish bounded component/file views and verify a sink/filter observes exact lengths without over-read.
3. **Async retention** — publish views backed by caller storage, mutate/reuse the caller storage after the logging call, drain the async logger, and verify the sink receives the original copied bytes.
4. **Bounded typed pattern** — pass a non-NUL-terminated format pattern to `salts_log_typed` and verify correct rendering through the #261 formatter path.
5. **Format decorator lifetime** — verify the inner callback sees the decorator-produced message as a correctly bounded view.
6. **Metrics length** — verify byte accounting follows `message.len`.

The first test-only commit must be observed RED in exact-head CI before production implementation is committed.

## Migration Sequence

1. Add contract tests against the new public ABI; establish RED.
2. Change `salts_log_entry_t` and public logging signatures.
3. Update raw/typed producer paths and convenience macros.
4. Convert async storage to view-length-driven copying.
5. Convert pattern rendering and built-in sinks/decorators.
6. Convert filters, metrics, custom/callback tests and examples.
7. Update CMeta/reflection expectations and any C/C++ integration examples.
8. Run focused tlog tests.
9. Run exact-head C API notation checks.
10. Run exact-head full CMeta conformance on Linux, macOS, Windows, and Android.

## Acceptance Criteria

The phase is complete only when all of the following are true:

- no public `tlog` runtime entry field uses `const char *` for component/file/message;
- `salts_log_entry_t` has no redundant `message_len`;
- raw and typed logging accept bounded views;
- non-NUL-terminated component/file/message/pattern inputs are covered by tests;
- async logging copies exactly by supplied lengths and does not retain caller storage;
- sinks, filters, decorators, and metrics consume view lengths directly;
- no compatibility/fallback logging ABI is added;
- C API notation is GREEN;
- full CMeta conformance is GREEN on Linux, macOS, Windows, and Android.

## Non-Goals

- replacing the async queue/disruptor implementation;
- changing logger/sink ownership transfer semantics;
- redesigning log levels or CMeta enum handling;
- changing file rotation policy;
- converting all sink configuration/path strings to `vstr`;
- maintaining binary or source compatibility with the pre-migration `tlog` ABI.
