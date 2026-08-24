# CBind String/Bytes Decode Design

**Date:** 2026-08-24

**Status:** Approved for implementation

## Objective

Add the D3 decode slice for semantic `STRING` and `BYTES` values without
coupling `TurboUtils::CBind` to a concrete string or byte-buffer layout.
Support root values and struct fields with explicit owned or borrowed storage,
bounded payload size, and whole-root transactional rollback.

## Scope

This slice includes:

- root `CMETA_DATA_STRING` and `CMETA_DATA_BYTES` values;
- string/bytes fields in reflected structs;
- generic, versioned CMeta buffer storage adapters;
- standard TurboUtils adapters for unique-owned `tstr` and borrowed `vstr`;
- a per-value `max_buffer_bytes` CBind limit;
- exact token-kind, source-lifetime, target-error, and rollback behavior.

This slice does not include:

- string/bytes elements inside sequence, set, or map containers;
- enum, variant, optional, default, alias, or coercion policies;
- custom ownership adapters;
- UTF-8 revalidation in CBind;
- unbounded allocation or an implicit owned fallback for borrowed storage.

## Existing Boundaries

`CSerde` emits a borrowed `cserde_slice` for both string and byte tokens. A
`TRANSIENT` slice may become invalid on the next reader operation. A `STABLE`
slice survives reader advancement but is still owned by the reader's backing
owner.

`CMeta` describes semantic data and storage types but currently has no storage
operation contract for string or byte objects. `CBind` links only CMeta and
CSerde, so it cannot depend on `tstr`, `vstr`, or TurboUtils Core.

`tstr` is a unique-owned, binary-safe byte string. `vstr` is a borrowed
`{data,len}` view and never extends its source lifetime.

## Architecture

```text
CSerde token + lifetime
          |
          v
   CBind validation/decode
    |  exact kind/lifetime
    |  limit/error/rollback
    v
CMeta buffer adapter facade
          |
          v
 concrete provider (Core: tstr or vstr)
```

CMeta owns the adapter ABI and checked facade. CBind owns format-specific
reader rules. Core owns the concrete TurboUtils storage providers. This keeps
the dependency direction unchanged:

```text
CBind -> CMeta + CSerde
Core  -> CMeta
```

## Public CMeta Adapter Contract

Append a `buffer_ops` pointer to `cmeta_data_desc`. The existing prefix through
`shape` remains the minimum valid descriptor size so old descriptors remain
valid metadata. A string/bytes descriptor without accessible adapter metadata
is valid CMeta metadata but unsupported by CBind.

The versioned operation table is:

```c
enum { CMETA_DATA_BUFFER_OPS_ABI_VERSION = 1u };

typedef bool (*cmeta_data_buffer_is_zero_fn)(const void *object);
typedef cmeta_status (*cmeta_data_buffer_assign_fn)(
    void *object, const unsigned char *data, size_t size, size_t max_bytes);
typedef void (*cmeta_data_buffer_restore_zero_fn)(void *object);

typedef struct cmeta_data_buffer_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    cmeta_data_buffer_ownership ownership;
    cmeta_data_buffer_is_zero_fn is_zero;
    cmeta_data_buffer_assign_fn assign;
    cmeta_data_buffer_restore_zero_fn restore_zero;
} cmeta_data_buffer_ops;
```

The checked facade validates descriptor accessibility, operation-table
size/version, semantic storage-type equality, ownership equality, null/length
pairs, capacity, and the required callbacks before invoking a provider. Its
public operations are:

```c
const cmeta_data_buffer_ops *cmeta_data_buffer_ops_of(
    const cmeta_data_desc *desc);
cmeta_status cmeta_data_buffer_is_zero(
    const cmeta_data_desc *desc, const void *object, bool *out);
cmeta_status cmeta_data_buffer_assign(
    const cmeta_data_desc *desc, void *object,
    const unsigned char *data, size_t size, size_t max_bytes);
cmeta_status cmeta_data_buffer_restore_zero(
    const cmeta_data_desc *desc, void *object);
```

`assign` has a strict precondition: the destination is in the provider's
semantic zero state. `size > max_bytes` returns `CMETA_CAPACITY_EXCEEDED`
before the provider mutates the object. A failed provider callback must leave
it in that state. `restore_zero` accepts either zero or live state and releases
any owned resource. These operations are single-owner control operations and
provide no internal synchronization.

The adapter is format-neutral. It does not mention CSerde lifetime or text
encoding.

## Standard TurboUtils Providers

Add `turbo_cmeta_data.h` with header-local immutable descriptors and operation
tables:

- `turbo_tstr_cmeta_type` / `turbo_tstr_cmeta_buffer_ops`;
- `turbo_vstr_cmeta_type` / `turbo_vstr_cmeta_buffer_ops`.

The stable type identities are `turbo.tstr` and `turbo.vstr`. Header-local
metadata follows CMeta's existing multi-translation-unit model and remains
usable in static C initializers, including when Core is a Windows DLL. Type
identity is semantic; consumers must not compare descriptor addresses across
translation units.

For `tstr`:

- semantic zero is `NULL`;
- assignment copies the exact bytes with `tstr_new_len`;
- embedded NUL bytes are preserved;
- restore uses `tstr_freep`;
- allocation failure returns `CMETA_OUT_OF_MEMORY` and leaves `NULL`.

For `vstr`:

- semantic zero is `{NULL, 0}`;
- non-empty assignment borrows the supplied address and length;
- empty assignment canonicalizes to `{NULL, 0}`;
- restore writes `{NULL, 0}` and never frees the source.

The same provider may back semantic STRING and BYTES values because both
storage types are byte-oriented. The semantic descriptor kind distinguishes
text from bytes.

## CBind Context and Capacity

Append `size_t max_buffer_bytes` to `cbind_context`. Existing initializer
macros retain their source behavior and initialize the new tail to zero. Add:

```c
#define CBIND_CONTEXT_WITH_BUFFERS_INIT( \
    scratch_ptr, scratch_bytes, depth_limit, item_limit, buffer_limit) ...
```

A graph containing string/bytes storage requires a context whose `struct_size`
reaches `max_buffer_bytes`; an old context prefix is rejected with
`CBIND_INVALID_CONTEXT` before input is read. The zero default permits only an
empty payload. Each individual decoded buffer is bounded by
`max_buffer_bytes`; the schema fixes the number of struct fields.

The size check occurs before the adapter callback. `size > max_buffer_bytes`
returns `CBIND_LIMIT_EXCEEDED` and leaves the destination zero. No fallback,
truncation, or replacement allocation is attempted.

## Decode Rules

Preflight validates the entire semantic graph before reading input:

- descriptor and reflected storage size/alignment must match;
- a usable adapter must be present;
- adapter storage type and ownership must match the descriptor;
- `CMETA_DATA_BUFFER_CUSTOM` is unsupported;
- string/bytes inside a container remains unsupported;
- the new context tail must be accessible when the graph uses buffers.

Decode then requires an exact token kind:

- STRING accepts only `CSERDE_STRING`;
- BYTES accepts only `CSERDE_BYTES`.

Owned storage copies either `TRANSIENT` or `STABLE` tokens. Borrowed storage
accepts only `STABLE`. Rejecting `TRANSIENT` borrowed assignment is mandatory:
otherwise a struct decoded before the next reader operation could retain a
dangling pointer.

## Transaction and Error Mapping

The root destination must be semantically zero before decode. On any failure,
CBind restores the entire root to semantic zero. For a struct this recursively
releases already assigned owned buffers and clears borrowed views alongside
the existing scalar/container cleanup.

Error mapping is:

| Condition | CBind status | target_status |
|---|---|---|
| payload exceeds limit | `CBIND_LIMIT_EXCEEDED` | `CMETA_CAPACITY_EXCEEDED` |
| provider reports capacity | `CBIND_LIMIT_EXCEEDED` | exact provider status |
| provider OOM | `CBIND_TARGET_ERROR` | `CMETA_OUT_OF_MEMORY` |
| other provider failure | `CBIND_TARGET_ERROR` | exact provider status |
| wrong token kind | `CBIND_TYPE_MISMATCH` | `CMETA_OK` |
| borrowed transient token | `CBIND_UNSUPPORTED` | `CMETA_OK` |

Missing or malformed adapter metadata is rejected before input. The error
shape and field point to the exact failing semantic value.

## Compatibility

- `cmeta_data_desc` and `cbind_context` grow only by appended fields.
- Their ABI version constants remain unchanged because the existing prefix is
  still interpreted identically through `struct_size`.
- Existing descriptor initializers remain valid; CMeta does not require an
  adapter for metadata validity.
- Existing CBind context macros remain source compatible and set the new
  limit to zero.
- Existing scalar, struct, and container behavior is unchanged.
- CBind's link dependencies do not change.

## Verification

Tests must cover:

- CMeta adapter ABI validation and checked facade behavior;
- C and C++ public-header compilation;
- Core `tstr` copy/embedded-NUL/OOM-shape cleanup behavior;
- Core `vstr` borrow/empty/reset behavior;
- root and struct STRING/BYTES decode;
- owned transient and stable sources;
- borrowed stable success and transient rejection;
- exact-kind mismatch, zero/exact/over-limit sizes;
- non-empty destination rejection;
- provider error attribution and whole-root rollback;
- preflight rejection without consuming input;
- all existing focused CMeta, Core, CSerde, and CBind regressions;
- Release configure/build/test/install and an installed-header consumer check.
