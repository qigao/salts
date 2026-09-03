# CBind Enum and Variant Decode Design

## 1. Status and scope

This document locks the D4 decode contract for `CMETA_DATA_ENUM` and
`CMETA_DATA_VARIANT`. It extends the existing forward-only CSerde reader and
empty-destination transaction model; it does not add a parser, DOM, encoder,
replace API, or format-specific projection policy.

The implementation remains in Salts:

```text
CMeta   owns semantic shape and native-storage lifecycle adapters
CSerde  owns canonical tokens
CBind   owns token-to-native validation and transaction semantics
TurboParser owns JSON/YAML/XML/CSV projection into CSerde tokens
```

## 2. Evidence from the current tree

- `cmeta_data_enum_shape` exposes reflection metadata but no portable way to
  read or write an implementation-defined C enum representation.
- `cmeta_data_variant_shape` exposes tag/case offsets but no active-case
  lifecycle. Writing a discriminator before initializing a union payload can
  leave an object that cannot be safely reset.
- `cbind_decode()` requires a semantic-zero destination and restores semantic
  zero on every post-consumption failure.
- CSerde v1 has scalar, map, and array tokens, but no variant-specific token.
- `cmeta_data_desc.struct_size` already supports append-only capability fields;
  the buffer adapter establishes the checked-facade pattern.

Therefore D4 must add storage adapters before enabling decode. Raw `memcpy`
based enum/union guesses are not a valid fallback.

## 3. Public CMeta ABI extension

`cmeta_data_desc` keeps ABI version 1 and appends two optional pointers:

```c
const cmeta_data_enum_ops *enum_ops;
const cmeta_data_variant_ops *variant_ops;
```

Old descriptors remain valid because generic descriptor validation continues
to require only the existing prefix ending at `shape`. A CBind-supported enum
or variant must expose the corresponding appended pointer and a complete v1
adapter. Consumers inspect each field only after checking `struct_size`.

Both adapter records use `struct_size`, `abi_version`, and a semantic
`storage_type` match. Semantic type equality alone is insufficient: kind,
size, and alignment must also match exactly.

### 3.1 Enum adapter

```c
typedef struct cmeta_data_enum_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    bool (*is_zero)(const void *object);
    cmeta_status (*read)(const void *object, int64_t *out);
    cmeta_status (*assign)(void *object, int64_t value);
    void (*restore_zero)(void *object);
} cmeta_data_enum_ops;
```

Checked facades are provided for lookup, zero query, read, assign, and reset.
`assign` requires semantic zero and a value declared by the referenced
`cmeta_enum_desc`. It verifies the resulting value through `read`. Callback
failure invokes `restore_zero` before returning.

Enum zero is a provider-defined construction state. It may also be a declared
enumerator; as with existing scalar zero, successful decode to zero remains a
valid semantic value even though a later empty-destination check sees zero.

### 3.2 Variant adapter

```c
typedef struct cmeta_data_variant_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_type_desc *storage_type;
    bool (*is_zero)(const void *object);
    cmeta_status (*active_tag)(const void *object, int64_t *out);
    cmeta_status (*select)(void *object, int64_t tag);
    void (*restore_zero)(void *object);
} cmeta_data_variant_ops;
```

`select` is the lifecycle boundary. It requires semantic zero and must:

1. initialize the selected payload to the zero state required by its semantic
   descriptor;
2. establish the active discriminator;
3. leave the object safely resettable by `restore_zero`.

The checked facade rejects an unknown case before calling the provider. After
success it requires `is_zero == false` and `active_tag == requested_tag`.
Failure or postcondition violation calls `restore_zero`; a violated
postcondition is `CMETA_CALLBACK_ERROR`.

`restore_zero` destroys/releases the active payload when necessary, clears the
discriminator/engagement state, and must make `is_zero` true. This single
provider-owned operation is the only rollback fact source.

## 4. Canonical enum input

CBind enum decode accepts:

- `CSERDE_STRING` equal byte-for-byte to an item's `text` or `symbol`;
- `CSERDE_SINT` equal to a declared item value;
- `CSERDE_UINT` not greater than `INT64_MAX` and equal to a declared item
  value.

`CSERDE_FLOAT` is rejected even when integral. A wrong token category produces
`CBIND_TOKEN_MISMATCH`; an unknown string, undeclared integer, or oversized
unsigned integer produces `CBIND_VALUE_OUT_OF_RANGE`.

Accepting all three exact forms is the fixed D4 decode grammar. Future encode
policy may choose text, symbol, or integer output without changing this reader
contract. String slices are compared without retaining or requiring NUL
termination.

## 5. Canonical variant input

D4 uses an exact two-element array:

```text
ARRAY_BEGIN
    tag
    payload
ARRAY_END
```

This representation is ordered, streamable, and format-neutral. It does not
reserve JSON field names or make map decoding order-dependent.

Tag decoding follows the tag descriptor:

- `ENUM`: the enum grammar in section 4, using metadata only;
- `SINT`: `CSERDE_SINT`, or `CSERDE_UINT <= INT64_MAX`;
- `UINT`: non-negative `CSERDE_SINT`, or `CSERDE_UINT <= INT64_MAX`.

Float tags are rejected. The resolved `int64_t` must name a variant case and
fit the declared integer width. This preserves the existing v1 case-tag domain
`[INT64_MIN, INT64_MAX]`, with unsigned tags limited to `[0, INT64_MAX]`.

Missing elements, extra elements, and wrong delimiters fail explicitly. The
reader remains forward-only and is never rewound or silently drained.

## 6. Validation and supported graphs

Preflight validates the complete reachable graph before reading input:

- adapter field accessibility, adapter ABI, and storage identity;
- tag kind and case-tag representability;
- tag and payload offsets against the variant storage size using checked
  subtraction;
- every case payload descriptor;
- cycles across struct and variant edges;
- maximum aggregate depth;
- maximum simultaneously active struct bitmap scratch.

Variant cases may contain supported scalar, enum, buffer, struct, or variant
values. A struct case may contain the already-supported Container containers.
Direct container cases remain unsupported in D4 because
`cmeta_data_variant_case` has no declared-container type metadata; preflight
fails before reader consumption. This is explicit rather than a guessed
storage fallback.

## 7. Decode transaction

For each variant value CBind performs:

1. read and validate `ARRAY_BEGIN`;
2. read and resolve the tag;
3. call checked `variant_select`;
4. verify the selected payload is semantic zero;
5. prepare nested struct containers when the payload is a struct;
6. decode the payload in place;
7. require `ARRAY_END`.

Any failure after selection is handled by the existing outer rollback path,
which reaches `variant_restore_zero` for root, struct-field, or outer-variant
payloads. Provider failures surface as `CBIND_TARGET_ERROR` with the exact
`cmeta_status` stored in `cbind_error.target_status`.

## 8. Compatibility and migration

- No existing token value, status value, or function signature changes.
- Existing descriptor initializers and prefix-sized descriptors remain valid.
- Existing enum/variant descriptors remain valid CMeta metadata but are
  `CBIND_UNSUPPORTED` until they attach complete ops.
- `Salts::CBind` keeps production dependencies limited to CMeta + CSerde.
- No file under `utils/vendor` is changed.
- TurboParser integration remains a later, separate repository PR consuming
  installed public Salts targets only.

## 9. Verification

Required tests cover:

- CMeta adapter ABI/storage mismatch and checked rollback;
- enum text, symbol, signed, and unsigned success;
- enum transient slice handling, unknown values, wrong tokens, callback
  failure, and non-empty destination;
- variant scalar, buffer, struct-with-container, and nested-variant cases;
- tag range, unknown case, wrong/missing/extra tokens;
- provider select failure and postcondition violation;
- rollback after payload/source/end-token failures;
- struct/variant cycles, depth, scratch, offset overflow, and unsupported direct
  container cases;
- C11 and C++17 public-header compilation;
- existing CMeta/CSerde/CBind regression tests.
