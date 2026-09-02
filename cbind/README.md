# CBind

CBind is the C11, format-neutral decode kernel between a canonical CSerde
reader and CMeta-described native storage. The production target
`Rocida::CBind` depends only on `Rocida::CMeta` and
`Rocida::CSerde`.

## STRING and BYTES storage

CBind decodes root STRING/BYTES values and struct fields through the buffer
adapter attached to `cmeta_data_desc`. It does not know concrete `tstr`,
`vstr`, or other application layouts.

The required rules are:

- STRING accepts exactly `CSERDE_STRING`; BYTES accepts exactly
  `CSERDE_BYTES`.
- Owned adapters copy either transient or stable source slices.
- Borrowed adapters accept only `CSERDE_VIEW_STABLE` slices.
- `max_buffer_bytes` is a hard limit for each decoded value.
- The destination must be in adapter-defined semantic zero state.
- Any failure restores the complete root semantic graph to zero.
- CBind never rewinds the reader after failure.

Use `CBIND_CONTEXT_WITH_BUFFERS_INIT(scratch, scratch_size, max_depth,
max_container_items, max_buffer_bytes)` for any graph containing STRING or
BYTES. Older context prefixes remain valid for older scalar graphs, but a
buffer graph rejects them before consuming input.

TurboUtils applications can include `turbo_cmeta_data.h` and attach
`turbo_tstr_cmeta_buffer_ops` for unique-owned storage or
`turbo_vstr_cmeta_buffer_ops` for borrowed storage. A borrowed decoded view
remains valid only as long as the CSerde reader's backing owner keeps the
stable slice alive.

The executable behavior examples are maintained with the implementation in
[`tests/cbind_buffer_decode_test.c`](tests/cbind_buffer_decode_test.c). They
cover complete descriptors, context setup, root and struct decode, embedded
NUL bytes, lifetime rejection, limits, target failures, and rollback.

## ENUM storage

CBind decodes an enum only when its descriptor exposes a complete
`cmeta_data_enum_ops` adapter. Input may be an exact reflected `text`, exact
`symbol`, declared signed integer, or declared unsigned integer not greater
than `INT64_MAX`. Float coercion and undeclared values are rejected. String
slices are compared directly and never retained.

Provider assignment failures surface as `CBIND_TARGET_ERROR` and restore the
adapter-defined semantic-zero state. See
[`tests/cbind_enum_decode_test.c`](tests/cbind_enum_decode_test.c) for complete
descriptor and rollback examples.

## VARIANT storage

CBind represents a tagged variant as an exact two-element canonical array:

```text
ARRAY_BEGIN, tag, payload, ARRAY_END
```

The descriptor must expose `cmeta_data_variant_ops`. Its `select` callback
initializes and engages a declared case; `restore_zero` owns destruction of the
complete active payload. CBind checks the selected tag and empty payload before
decoding, so malformed providers fail as `CBIND_TARGET_ERROR` instead of
leaving a half-active union.

Integer discriminators use exact SINT/UINT tokens in the signed 64-bit case-tag
domain. Enum discriminators additionally accept reflected text or symbol.
Scalar, enum, buffer, struct, and nested-variant payloads are supported. Direct
container cases are rejected before input because variant case metadata does
not yet carry a declared container type; containers nested in a struct case
remain supported.

See [`tests/cbind_variant_decode_test.c`](tests/cbind_variant_decode_test.c) for
preflight, lifecycle, nested payload, resource-limit, and rollback examples.

String/bytes/enum container elements and custom buffer ownership remain
unsupported and fail without an implicit fallback.
