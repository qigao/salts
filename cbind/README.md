# CBind

CBind is the C11, format-neutral decode kernel between a canonical CSerde
reader and CMeta-described native storage. The production target
`TurboUtils::CBind` depends only on `TurboUtils::CMeta` and
`TurboUtils::CSerde`.

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

String/bytes/enum container elements, custom buffer ownership, and variant
decoding remain unsupported in this slice and fail without an implicit
fallback.
