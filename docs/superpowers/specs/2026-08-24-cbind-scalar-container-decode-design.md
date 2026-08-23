# CBind Scalar Container Decode Design

**Date:** 2026-08-24

## 1. Objective

Extend the existing CBind scalar + struct decode kernel so reflected `Struct(...)`
fields declared with `TYPE(...)` can decode scalar containers from CSerde tokens.
The extension covers:

- sequence fields with one scalar element type;
- set fields with one scalar element type;
- map fields with scalar key and scalar value types;
- nested structs containing those fields.

The supported scalar set remains exactly the D2 canonical set: `bool`, `int`,
`long`, `size_t`, `float`, and `double`.

This slice does not add root containers, strings/bytes, enum/variant values,
struct elements, nested containers, heap-like containers, or multimap semantics.

## 2. Existing contracts that remain stable

- `cbind_decode()` still performs graph and resource preflight before reading the
  first source token.
- The destination must be semantically empty before decode.
- Any failure after destination preparation restores the complete root object to
  its canonical empty state.
- Unknown, duplicate, and missing struct fields keep the D2 behavior.
- CBind production code depends only on CMeta and CSerde. TurboSTL remains one
  provider used by tests and consumers.
- Existing `cbind_context`, `cbind_error`, and status values retain their ABI-v1
  prefixes and numeric meanings.

## 3. Public API extensions

### 3.1 Bounded container policy

Append `size_t max_container_items` to `cbind_context`. One item means one
sequence/set value or one map key/value pair. The limit is per container.

`CBIND_CONTEXT_INIT(scratch, bytes, depth)` remains source compatible and sets
the appended value to zero. Add:

```c
#define CBIND_CONTEXT_WITH_CONTAINERS_INIT(                              \
    scratch_ptr, scratch_bytes, depth_limit, item_limit)                 \
  {sizeof(cbind_context), CBIND_ABI_VERSION, (scratch_ptr),              \
   (scratch_bytes), (depth_limit), (item_limit)}
```

A graph containing a container requires a context prefix through
`max_container_items`. A zero item limit is valid and permits only empty input
containers. Scalar/struct-only callers continue to require only the old prefix.

### 3.2 Target error attribution

Append `cmeta_status target_status` to `cbind_error`. Add
`CBIND_TARGET_ERROR` at the end of `cbind_status`.

- `CMETA_CAPACITY_EXCEEDED` maps to `CBIND_LIMIT_EXCEEDED` while retaining the
  exact status in `target_status`.
- Other provider/Collector failures map to `CBIND_TARGET_ERROR` with the exact
  CMeta status.
- Preflight trait, arity, type, and lifecycle mismatches are CBind shape errors,
  not runtime target errors.

An older error prefix remains accepted. CBind writes `target_status` only when
the caller-provided prefix includes that field.

### 3.3 Container restore-to-zero lifecycle

Append this callback to `cmeta_container_construct_ops`:

```c
typedef void (*cmeta_container_restore_zero_fn)(void *object);

typedef struct cmeta_container_construct_ops {
  size_t struct_size;
  uint32_t abi_version;
  const cmeta_container_desc *descriptor;
  cmeta_container_bind_types_fn bind_types;
  cmeta_container_restore_zero_fn restore_zero;
} cmeta_container_construct_ops;
```

Expose the checked façade:

```c
cmeta_status cmeta_container_restore_zero(
    void *object, const cmeta_declared_type *declared);
```

The callback accepts a bound-uninitialized or live container, releases any
owned resources, and leaves the complete declared storage as the provider's
canonical all-bits-zero handle. It is idempotent for a zero handle. Existing
providers with the shorter ops prefix remain valid for binding; CBind container
preflight rejects them before source consumption because transactional rollback
cannot otherwise be guaranteed.

This is separate from `cmeta_collector_abort()`: Collector abort owns an active
collection transaction and is intentionally a no-op after commit, whereas root
rollback must also destroy containers committed before a later field fails.

## 4. Shape and declaration validation

Container semantic descriptors remain format-neutral categories without a
concrete storage type. Therefore CBind validates each container field using both
metadata sources:

1. the CBind semantic field shape identifies sequence, set, or map semantics;
2. the matching reflected `cmeta_field_desc` supplies `declared_type`;
3. `declared_type->construction` supplies the concrete provider;
4. its descriptor must expose the matching CMeta category and Collector;
5. sequence/set arity must be one and map arity must be two;
6. every declared argument must equal one of the canonical scalar type
   descriptors;
7. reflected storage size/alignment must match the declared storage type;
8. the construction ops must include `bind_types` and `restore_zero`.

All checks occur recursively during graph preflight. Unsupported container kinds
and element types fail without consuming input or changing the destination.

Root container decoding is deliberately absent because `cbind_decode(shape, ...)`
does not carry a `cmeta_declared_type`; guessing a concrete provider from a
semantic category would violate the declaration-side construction contract.

## 5. State, ownership, and transaction protocol

The caller owns the root destination. CBind is single-threaded and requires
external serialization if the destination is shared.

Container fields use this state machine:

```text
ZERO --bind_types--> BOUND --collector.begin--> ACCEPTING
ACCEPTING --collector.finish--> COMMITTED
BOUND | ACCEPTING | COMMITTED --restore_zero--> ZERO
```

Execution order is:

1. validate the complete graph and compute scratch requirements;
2. verify the complete destination is empty, including bytewise-zero container
   storage;
3. bind every container field from its reflected declaration;
4. if any bind fails, restore every visited container field to zero;
5. read and decode tokens;
6. on any decode failure, recursively reset scalars/structs and restore every
   container field to zero;
7. on success, leave committed containers owned by the caller.

Binding is allocation-free. Provider allocations may occur inside Collector
accept and are bounded by `max_container_items`. Source string slices and local
scalar values are borrowed only until `collector.accept()` returns; a conforming
Collector copies or otherwise acquires the value before returning success.

No rollback journal is needed: the validated reflection graph is the single
fact source and can deterministically revisit every container field.

## 6. Token algorithms

### 6.1 Sequence and set

- require `CSERDE_TOKEN_ARRAY_BEGIN`;
- read tokens until `CSERDE_TOKEN_ARRAY_END`;
- before decoding a non-end token, fail with `CBIND_LIMIT_EXCEEDED` when the
  accepted count already equals `max_container_items`;
- decode the already-read token through the existing strict scalar conversion;
- pass the local canonical scalar to `collector.accept()`;
- finish on the matching end token.

### 6.2 Map

- require `CSERDE_TOKEN_MAP_BEGIN`;
- read a key token or `CSERDE_TOKEN_MAP_END`;
- enforce the pair limit before decoding the key;
- decode key and then read/decode its scalar value;
- pass a `cmeta_entry` containing the declared key/value types and borrowed local
  scalar addresses to `collector.accept()`;
- finish on the matching end token.

The scalar decoder is split into a token-consuming wrapper and a helper that
decodes an already-read token. This avoids pushback and keeps D2 numeric
conversion semantics as one implementation.

## 7. Scratch, depth, and failure semantics

The D2 seen-field bitmap remains the only CBind scratch requirement; container
locals are bounded stack values. `max_depth` continues to count semantic struct
recursion. Containers in this slice cannot nest, so it does not gain a second
structural-depth meaning.

Malformed delimiters and scalar token mismatches return `CBIND_TOKEN_MISMATCH`.
Source failures retain `CBIND_SOURCE_ERROR`. Provider capacity and target errors
are attributed as described in section 3.2. In all post-preparation failures,
the observable destination after return is canonical empty.

## 8. Compatibility and impact

- **Public source compatibility:** existing aggregate macros and status values
  remain usable; new fields are appended.
- **Provider compatibility:** old CMeta construction providers still bind through
  the old prefix, but cannot participate in transactional CBind container decode.
- **Data format:** no CSerde token or external serialized representation changes.
- **Linkage:** CBind keeps CMeta + CSerde production dependencies; TurboSTL gains
  implementations for its existing concrete construction providers.
- **Migration cost:** container consumers opt into the four-argument context
  macro and receive an explicit per-container bound.

## 9. Verification

Tests must prove:

- vector/set/map success for every relevant scalar conversion path;
- empty containers and exact-limit success;
- limit+1 rejection without partial destination state;
- malformed tokens, source failures, Collector failures, and OOM/capacity status
  attribution;
- a committed earlier container is destroyed when a later field fails;
- nested struct container fields work;
- unsupported arity/type/provider and short context fail before reader calls;
- old scalar/struct contexts and old error prefixes retain D2 behavior;
- C and C++ public headers compile;
- CMeta restore-zero works for zero, bound, live, and committed handles and is
  idempotent.
