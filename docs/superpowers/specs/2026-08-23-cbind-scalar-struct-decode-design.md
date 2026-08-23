# CBind Scalar + Struct Decode Core Design

Date: 2026-08-23
Status: Design / Approved in chat, pending written-spec review
Base: `master@4216bb8c71cf6564bf88fe6cff3c8a1c227c87d3`
Scope: D2 — format-neutral decode-only POD scalar + struct kernel

## 1. Context

TurboUtils now has the prerequisites for a real format-neutral binding layer:

- CMeta provides stable type identity, semantic `cmeta_data_desc`, reflected struct layout, and generic container construction contracts.
- CSerde provides a standalone canonical token model plus versioned pull-reader / push-writer facades.
- CSerde intentionally owns no JSON/YAML/XML/CSV parser.
- TurboParser remains the future native-format adapter host.

The next boundary is CBind:

```text
native parser adapter
        |
        v
cserde_reader
        |
        v
      CBind
        |
        v
native C object
```

D2 deliberately implements only the smallest binding kernel whose memory semantics can already be proven from current CMeta metadata. It does not guess storage layouts or ownership rules that CMeta does not yet expose.

## 2. Goals

D2 must:

1. add a standalone `TurboUtils::CBind` C11 module;
2. depend only on `TurboUtils::CMeta + TurboUtils::CSerde`;
3. decode canonical CSerde tokens into supported native C storage;
4. support bool, signed integer, unsigned integer, floating point, and nested semantic structs;
5. perform precise numeric conversion with explicit range / exactness checks;
6. reject malformed or unsupported semantic graphs before consuming reader input;
7. require destination semantic fields to be in a defined empty state before consuming reader input;
8. guarantee destination rollback to that empty state after any post-consumption failure;
9. use caller-supplied scratch space for struct bookkeeping, with no hidden allocation and no fixed field-count ceiling;
10. provide append-safe versioned context and error records;
11. keep reader consumption streaming and forward-only; CBind does not rewind or silently skip after failure;
12. compile and link from both C11 and C++17;
13. extend Linux and Windows conformance CI with `cbind_*` tests.

## 3. Non-goals

D2 does not implement:

- encode;
- string or bytes binding;
- enum binding;
- variant/union binding;
- sequence, set, or map binding;
- custom semantic adapters;
- object replacement / `decode_replace`;
- unknown-field skip policy;
- aliases, defaults, optional fields, or rename policy;
- allocator callbacks;
- TurboParser adapters;
- CFlow integration;
- DOM/value-tree intermediates;
- generated per-type binding facades.

Any semantic kind outside the D2 supported set must fail explicitly with `CBIND_UNSUPPORTED` rather than being approximated.

## 4. Module layout

D2 introduces:

```text
cbind/
  CMakeLists.txt
  include/cbind/
    cbind.h
    status.h
    context.h
    error.h
    decode.h
  src/
    decode.c
    scalar.c
    struct.c
    internal.h
  tests/
    CMakeLists.txt
    cbind_scalar_decode_test.c
    cbind_struct_decode_test.c
    cbind_header_cpp_test.cpp
```

CMake target:

```text
concrete target: turbo_cbind
alias:           TurboUtils::CBind
export name:     CBind
```

Production dependency direction is fixed:

```text
TurboUtils::CBind
  -> TurboUtils::CMeta
  -> TurboUtils::CSerde
```

CBind must not link TurboSTL, Core/utils, CFlow, TurboParser, or parser libraries.

The root module order should place CBind after CSerde and before consumers that may later use it:

```cmake
add_subdirectory(cmeta)
add_subdirectory(cserde)
add_subdirectory(cbind)
add_subdirectory(cflow)
```

## 5. Supported semantic kinds

D2 supports exactly:

```text
CMETA_DATA_BOOL
CMETA_DATA_SINT
CMETA_DATA_UINT
CMETA_DATA_FLOAT
CMETA_DATA_STRUCT
```

D2 returns `CBIND_UNSUPPORTED` for valid descriptors of these kinds:

```text
CMETA_DATA_STRING
CMETA_DATA_BYTES
CMETA_DATA_ENUM
CMETA_DATA_VARIANT
CMETA_DATA_SEQUENCE
CMETA_DATA_SET
CMETA_DATA_MAP
CMETA_DATA_CUSTOM
```

The reason is structural, not temporary convenience: current CMeta STRING/BYTES metadata describes semantic ownership class but does not yet provide generic native storage access/lifecycle operations. Supporting those types now would force CBind to know concrete `tstr`, `vstr`, or application-specific layouts, violating the intended layer boundary.

## 6. Public status model

D2 introduces:

```c
typedef enum cbind_status {
    CBIND_OK = 0,
    CBIND_INVALID_ARGUMENT,
    CBIND_INVALID_CONTEXT,
    CBIND_INVALID_SHAPE,
    CBIND_DESTINATION_NOT_EMPTY,
    CBIND_TOKEN_MISMATCH,
    CBIND_VALUE_OUT_OF_RANGE,
    CBIND_UNKNOWN_FIELD,
    CBIND_DUPLICATE_FIELD,
    CBIND_MISSING_FIELD,
    CBIND_UNEXPECTED_END,
    CBIND_LIMIT_EXCEEDED,
    CBIND_UNSUPPORTED,
    CBIND_SOURCE_ERROR
} cbind_status;
```

Semantics:

- `CBIND_OK`: decode completed successfully.
- `CBIND_INVALID_ARGUMENT`: required pointer or caller-owned output contract is invalid; malformed optional `cbind_error` also maps here.
- `CBIND_INVALID_CONTEXT`: context prefix/version/scratch relationship is invalid.
- `CBIND_INVALID_SHAPE`: CMeta semantic graph is malformed or its claimed semantic storage is inconsistent with reflected C storage.
- `CBIND_DESTINATION_NOT_EMPTY`: destination semantic graph is not in the D2 empty state.
- `CBIND_TOKEN_MISMATCH`: reader supplied a valid canonical token that is incompatible with the expected semantic value.
- `CBIND_VALUE_OUT_OF_RANGE`: canonical numeric value cannot be converted under D2 strict numeric rules.
- `CBIND_UNKNOWN_FIELD`: canonical struct map contains an unknown field name.
- `CBIND_DUPLICATE_FIELD`: canonical struct map contains the same known field more than once.
- `CBIND_MISSING_FIELD`: a required semantic struct field was not present before `MAP_END`.
- `CBIND_UNEXPECTED_END`: reader returned normal CSerde `DONE` while CBind still required a token for the current value.
- `CBIND_LIMIT_EXCEEDED`: context depth or scratch budget cannot support the semantic graph.
- `CBIND_UNSUPPORTED`: descriptor is valid but belongs to a semantic kind/storage capability intentionally outside D2.
- `CBIND_SOURCE_ERROR`: CSerde reader returned a non-`DONE` failure; exact source status is preserved in `cbind_error`.

CBind never silently re-labels a reader/provider failure as a binder numeric/type error.

## 7. Context-first API convention

CBind uses context-first public APIs. The context is not an incidental option bag; it is the execution environment for the binding operation.

Public decode entry:

```c
cbind_status cbind_decode(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    void *out,
    cbind_error *error);
```

This convention is reserved for future symmetric APIs:

```text
cbind_decode(ctx, shape, reader, out, error)
cbind_encode(ctx, shape, object, writer, error)
cbind_decode_replace(ctx, shape, reader, out, error)
```

D2 implements only `cbind_decode`.

## 8. Versioned context

D2 context:

```c
enum { CBIND_CONTEXT_ABI_VERSION = 1u };

typedef struct cbind_context {
    size_t struct_size;
    uint32_t abi_version;
    void *scratch;
    size_t scratch_size;
    size_t max_depth;
} cbind_context;
```

The v1 required prefix ends at `max_depth` and is validated with field-end size, never `sizeof(current_struct)` as the minimum compatible size.

Because an out-of-line initializer compiled into a future library could otherwise write a larger future struct into an older caller's object, context initialization must be caller-size-aware. D2 therefore exposes a header-side initializer value/maker rather than an exported function that writes `sizeof(library-side cbind_context)`.

Normative initializer shape:

```c
#define CBIND_CONTEXT_INIT(scratch_ptr, scratch_bytes, depth_limit) \
    { sizeof(cbind_context), CBIND_CONTEXT_ABI_VERSION, \
      (scratch_ptr), (scratch_bytes), (depth_limit) }
```

A C/C++ compatible header-side `static inline` maker may be provided in addition to the macro, but it must derive `struct_size` from the caller's compilation unit.

Context validation:

```text
context != NULL
struct_size >= field_end(cbind_context, max_depth)
abi_version == CBIND_CONTEXT_ABI_VERSION
scratch_size == 0  => scratch may be NULL or non-NULL
scratch_size > 0   => scratch must be non-NULL
```

There is no process-global allocator, policy, mutable default context, or hidden scratch arena.

`max_depth` semantics:

```text
scalar root        requires depth 0
root struct        enters depth 1
nested struct      enters depth 2, 3, ...
```

Therefore:

```text
max_depth = 0  permits scalar root only
max_depth = 1  permits one root struct but no nested struct
```

Future allocator / field / numeric policy fields may be appended to `cbind_context` while preserving the v1 prefix.

## 9. Versioned error record

D2 error output:

```c
enum { CBIND_ERROR_ABI_VERSION = 1u };

typedef struct cbind_error {
    size_t struct_size;
    uint32_t abi_version;
    cbind_status status;
    cserde_status source_status;
    const cmeta_data_desc *shape;
    const cmeta_data_field_desc *field;
    size_t depth;
} cbind_error;
```

Caller-side initialization:

```c
#define CBIND_ERROR_INIT \
    { sizeof(cbind_error), CBIND_ERROR_ABI_VERSION, CBIND_OK, \
      CSERDE_OK, NULL, NULL, 0u }
```

If `error != NULL`, `cbind_decode` validates the v1 field-end prefix and ABI before consuming the reader. A malformed error record causes `CBIND_INVALID_ARGUMENT` and is not written.

On success, a valid supplied error record is normalized to:

```text
status        = CBIND_OK
source_status = CSERDE_OK
shape         = NULL
field         = NULL
depth         = 0
```

On binder-originated failures:

```text
source_status = CSERDE_OK
```

On reader `DONE` while a value/token is still required:

```text
status        = CBIND_UNEXPECTED_END
source_status = CSERDE_DONE
```

On any other reader failure:

```text
status        = CBIND_SOURCE_ERROR
source_status = exact cserde_status returned by cserde_reader_next()
```

`shape` points to the semantic descriptor at the failure site, not necessarily the root descriptor.

`field` behavior:

- known-field decode failure: points to that canonical `cmeta_data_field_desc`;
- duplicate field: points to the duplicated canonical field descriptor;
- missing field: points to the first missing canonical field in semantic descriptor order;
- unknown field/non-string key/struct-level source error: NULL unless the error occurred while decoding an already-resolved known field.

`depth` is the number of currently entered semantic structs. Scalar root errors use 0; a scalar field of the root struct uses depth 1; a field inside a nested struct uses depth 2.

D2 never stores a CSerde transient field-name slice in `cbind_error`.

## 10. Validation order and no-consumption guarantee

Before the first call to `cserde_reader_next`, `cbind_decode` must complete:

```text
1. public argument / optional error validation
2. context validation
3. recursive D2 semantic graph + native storage preflight
4. required max-depth and scratch-budget preflight
5. destination empty-state validation
```

Any failure in those five stages must leave the reader untouched and produce zero provider calls.

Only after all five stages succeed may CBind consume canonical input.

This preflight is important because D2 structs have no optional fields: every valid decode eventually needs every semantic field. It is therefore valid to require enough depth/scratch budget for the complete supported graph before consuming input rather than failing halfway through a structurally valid value.

## 11. Semantic graph preflight

CMeta `cmeta_data_desc_valid()` remains intentionally shallow for recursive graphs. CBind must therefore perform a D2-specific recursive preflight before touching destination memory or input.

### 11.1 General requirements

For every visited descriptor:

```text
desc != NULL
cmeta_data_desc_valid(desc) == true
kind is either D2-supported or explicitly valid-but-unsupported
```

A valid but unsupported kind returns `CBIND_UNSUPPORTED`.

A malformed descriptor returns `CBIND_INVALID_SHAPE`.

### 11.2 Canonical scalar storage proof

D2 does not claim to generically assign arbitrary C integer or floating storage merely because a descriptor reports a bit width. Current CMeta has semantic width metadata but no general canonical-scalar assignment callback.

D2 writable storage is restricted to current canonical CMeta built-ins:

```text
CMETA_DATA_BOOL  -> cmeta_type_bool
CMETA_DATA_SINT  -> cmeta_type_int or cmeta_type_long
CMETA_DATA_UINT  -> cmeta_type_size
CMETA_DATA_FLOAT -> cmeta_type_float or cmeta_type_double
```

For SINT/UINT/FLOAT, the semantic width in the shape must equal the actual native storage width:

```text
shape.bits == storage_type->size * CHAR_BIT
```

This remains platform-correct:

- `long` may be 64-bit on LP64 Linux and 32-bit on Windows LLP64;
- `size_t` follows the active ABI;
- `float`/`double` must match the descriptor-proven widths.

A descriptor that names one of the canonical built-in storages but claims a conflicting width is malformed and returns `CBIND_INVALID_SHAPE`.

A valid semantic scalar descriptor that uses a different native storage type is not guessed bytewise; it returns `CBIND_UNSUPPORTED`.

### 11.3 Struct storage proof

For a `CMETA_DATA_STRUCT` descriptor, CBind requires:

```text
shape != NULL
shape->layout != NULL
storage_type != NULL
storage_type->size  == shape->layout->size
storage_type->align == shape->layout->align
```

For every semantic field:

1. field name is unique within the semantic shape;
2. it resolves to exactly one reflected `cmeta_field_desc` by name;
3. semantic field offset equals reflected field offset;
4. reflected field size equals child semantic storage size;
5. reflected field alignment equals child semantic storage alignment;
6. `offset + child_size` is overflow-safe and remains within parent storage size;
7. two semantic entries may not alias the same reflected field;
8. child semantic graph recursively passes D2 preflight.

Layout fields not represented in the semantic shape are allowed and are outside D2 binding/rollback semantics.

### 11.4 Recursive graph cycle detection

A malicious semantic descriptor graph must not cause infinite recursive validation.

Preflight uses an internal stack-linked DFS frame on the C call stack:

```text
current descriptor + pointer to parent validation frame
```

Before entering a child struct, CBind scans the active ancestor chain for the same semantic descriptor address. Encountering an active cycle returns `CBIND_INVALID_SHAPE`.

This needs no heap allocation and does not consume caller scratch.

The context depth limit is also checked before recursive entry, so descriptor preflight and runtime decode use the same structured-depth contract.

## 12. Scratch model

Scratch is bookkeeping only. It is not a staging copy of the native destination object.

Every active struct frame needs a seen-field bitmap of exactly:

```text
(field_count + 7) / 8 bytes
```

There is no 32-field or 64-field hard limit.

A frame allocation uses a simple bump cursor. On exit from a nested struct, the cursor rewinds to the value it had before that frame, so scratch is reused across sibling subtrees.

The exact D2 scratch requirement is:

```text
max over all struct nesting paths(
    sum((field_count_on_active_frame + 7) / 8)
)
```

Empty structs require zero bitmap bytes but still consume one structured depth level.

Preflight computes the required scratch for the complete supported graph before reader consumption. If:

```text
required_scratch > context->scratch_size
```

CBind returns `CBIND_LIMIT_EXCEEDED` with zero reader calls.

Runtime frame allocation must still bounds-check defensively, but a correct preflight means a valid graph/context pair cannot unexpectedly exhaust scratch after input consumption.

## 13. Destination empty-state contract

Base `cbind_decode` constructs a new value; it does not replace a live value.

D2 precondition:

```text
out semantic graph is empty
```

D2 semantic empty state:

```text
BOOL        false
SINT        0
UINT        0
FLOAT       numeric zero; +0.0 and -0.0 are both empty
STRUCT      every semantic field recursively empty
```

CBind must not compare the entire object representation with zero bytes. In particular:

```text
no memcmp(out, zero, sizeof(...))
```

Struct padding is irrelevant.

Layout fields that are not part of the semantic shape are not inspected by empty-state validation.

If any semantic field is non-empty, CBind returns `CBIND_DESTINATION_NOT_EMPTY` before consuming the reader.

## 14. Rollback contract

Once input consumption begins, any decode failure must restore the destination semantic graph to the D2 empty state.

D2 only supports non-owning POD-like scalar/struct semantics, so rollback does not need destructor or allocation tracking. It recursively resets all semantic fields:

```text
BOOL        false
SINT        0
UINT        0
FLOAT       +0.0 or another numeric zero representation
STRUCT      recursively reset semantic fields
```

Non-semantic layout fields remain untouched on both success and failure.

Because preflight already proved the graph/storage mapping and entry empty state, rollback may reset the whole supported semantic graph rather than maintaining a separate constructed-field ownership journal.

This transaction protects the destination only.

CBind does not rewind the reader and does not silently consume the remainder of the failed value:

```text
failure:
    destination -> empty
    reader      -> remains at the actual failure position/state
```

The caller must not assume the same streaming reader can retry the failed value from its beginning.

## 15. Scalar memory access

CBind avoids typed pointer aliasing assumptions for erased destinations.

For scalar writes:

1. convert the canonical token into a local native C value of the proven storage type;
2. copy exactly `sizeof(native_type)` bytes into the destination with `memcpy`.

For empty checks, CBind copies destination bytes into a local native value with `memcpy` and compares semantically.

For reset, CBind creates a native zero value and copies it with `memcpy`.

This avoids dereferencing an arbitrary `void *` as a guessed scalar type and avoids endian/representation guesses for unsupported native types.

## 16. Strict numeric conversion contract

D2 performs numeric conversion only among canonical numeric tokens and proven numeric CMeta storage. It does not implement string-to-number, bool-to-number, or other convenience coercions.

### 16.1 BOOL

`CMETA_DATA_BOOL` accepts only:

```text
CSERDE_BOOL
```

No integer `0/1`, strings, or null coercion is allowed.

### 16.2 Integer destinations

Signed integer destination:

```text
CSERDE_SINT -> signed target
    accept iff value is in target signed range

CSERDE_UINT -> signed target
    accept iff value <= target signed max

CSERDE_FLOAT -> signed target
    value must be finite
    value must be mathematically integral
    value must be in exact target signed range
```

Unsigned integer destination:

```text
CSERDE_UINT -> unsigned target
    accept iff value is in target unsigned range

CSERDE_SINT -> unsigned target
    accept iff value >= 0 and value is in target unsigned range

CSERDE_FLOAT -> unsigned target
    value must be finite
    value must be mathematically integral
    value must be in exact target unsigned range
```

No cast/truncation may occur before range/integrality has been proven.

A rejected conversion returns `CBIND_VALUE_OUT_OF_RANGE`.

### 16.3 Floating destinations

Canonical FLOAT -> native `double`:

```text
copy canonical binary64 value directly
NaN and +/-Inf are preserved
```

Canonical FLOAT -> native `float`:

```text
perform the normal binary64 -> binary32 conversion
NaN and +/-Inf are preserved as non-finite values
finite overflow to infinity => CBIND_VALUE_OUT_OF_RANGE
nonzero finite underflow that becomes zero => CBIND_VALUE_OUT_OF_RANGE
finite in-range precision rounding is allowed
```

Canonical SINT/UINT -> native float/double:

```text
accept only when the integer is exactly representable in the target floating type
otherwise CBIND_VALUE_OUT_OF_RANGE
```

Therefore values such as an integer just above binary64's exact-integer precision boundary are never silently rounded into a different integer-valued `double`.

### 16.4 Token kind mismatch

A scalar canonical token that is not in the semantic kind's accepted token set returns `CBIND_TOKEN_MISMATCH`, not `CBIND_VALUE_OUT_OF_RANGE`.

Examples:

```text
STRING -> SINT   TOKEN_MISMATCH
BOOL   -> FLOAT  TOKEN_MISMATCH
ARRAY  -> UINT   TOKEN_MISMATCH
```

## 17. Struct canonical representation

D2 struct input is exactly a canonical map with string keys:

```text
MAP_BEGIN
  STRING("field-a") value
  STRING("field-b") value
MAP_END
```

D2 strict field semantics are fixed:

```text
field matching      exact, case-sensitive
unknown field       reject
missing field       reject
optional field      not supported
alias                not supported
default              not supported
duplicate field      reject
non-string map key   reject
```

Every semantic field is required exactly once.

## 18. Struct decoder state machine

Normative state machine:

```text
expect MAP_BEGIN

allocate/zero seen bitmap for this frame

loop:
    take next token

    if MAP_END:
        find first unseen semantic field in descriptor order
        if any unseen field exists:
            MISSING_FIELD
        else:
            success

    else if token is STRING:
        find exact semantic field by borrowed slice bytes
        if no match:
            UNKNOWN_FIELD
        if already marked seen:
            DUPLICATE_FIELD
        recursively decode the field value
        if child succeeds:
            mark field seen
        continue

    else:
        TOKEN_MISMATCH
```

Key lookup must not require NUL termination:

```text
slice.size == strlen(field->name)
&&
memcmp(slice.data, field->name, slice.size) == 0
```

A zero-length field name is not a valid CMeta semantic field because descriptor validation already requires non-empty names.

Transient key views are used only during the current dispatch step and are never stored beyond the consuming reader operation.

## 19. Reader status mapping

All input is consumed through `cserde_reader_next()`.

When CBind requires a token:

```text
CSERDE_OK
    inspect valid canonical token

CSERDE_DONE
    CBIND_UNEXPECTED_END
    source_status = CSERDE_DONE

any other cserde_status
    CBIND_SOURCE_ERROR
    source_status = exact returned status
```

Examples of source failures include adapter/source `CSERDE_SOURCE_ERROR`, CSerde sticky failures, callback normalization, and source-side `CSERDE_VALUE_OUT_OF_RANGE`.

These are distinct from binder-originated `CBIND_VALUE_OUT_OF_RANGE` produced while converting a successfully delivered canonical numeric token.

## 20. Failure field/depth attribution

Error attribution must be deterministic:

- root scalar conversion mismatch/range failure: `shape=root`, `field=NULL`, `depth=0`;
- known root struct field scalar failure: `shape=child scalar`, `field=canonical root field`, `depth=1`;
- nested struct entry/key error: `shape=nested struct`, `field=parent field if already resolved for this nested value`, `depth=2`;
- unknown field at root: `shape=root struct`, `field=NULL`, `depth=1`;
- duplicate field: `shape=root/current struct`, `field=duplicated canonical field`, current struct depth;
- missing field: `shape=current struct`, `field=first missing canonical field`, current struct depth.

If a nested child produces an error, the outer decoder preserves that more specific child `shape/depth` while keeping the immediately relevant canonical field pointer available.

## 21. C++17 public surface

Public headers must be consumable from C++17:

- no public `_Generic` dependency;
- no GNU-only public declarations;
- exported callable declarations wrapped in standard `extern "C"` guards;
- context/error initializer forms compile in both C11 and C++17;
- `cbind_decode` links from C++17 to the C implementation.

## 22. Test infrastructure

CBind tests reuse the BUILD_TESTS-only CSerde recording provider introduced by D1:

```text
cserde_recording_reader_ops
cserde_recording_reader_context
```

CBind must not fork another recording token source.

`cserde_recording_support` remains test-only and is not linked into production `turbo_cbind`.

## 23. Required test matrix

### 23.1 Scalar decode

Tests must cover:

- BOOL true/false and non-BOOL mismatch;
- signed min/max and one-step overflow where representable by canonical source token;
- unsigned max and negative SINT -> UINT rejection;
- UINT -> signed exact range boundary;
- integral FLOAT -> integer success;
- fractional FLOAT -> integer rejection;
- NaN/Inf -> integer rejection;
- FLOAT -> double NaN/Inf preservation;
- double -> float finite overflow rejection;
- nonzero finite double -> float underflow-to-zero rejection;
- finite in-range double -> float rounding acceptance;
- SINT/UINT -> float/double exact-representability success/failure;
- token-kind mismatch vs numeric-range distinction.

Platform-dependent `int`, `long`, and `size_t` boundaries must use `<limits.h>` / standard width facts rather than assuming LP64 on Windows.

### 23.2 Storage proof

Tests must cover:

- built-in `bool/int/long/size_t/float/double` canonical descriptors accepted;
- forged semantic width conflicting with canonical built-in storage rejected as `CBIND_INVALID_SHAPE`;
- valid semantic scalar descriptor using a non-canonical native storage rejected as `CBIND_UNSUPPORTED`;
- struct storage size/alignment mismatch rejected before reader consumption;
- semantic/reflected field size, alignment, offset mismatch rejected before reader consumption;
- duplicate semantic field mapping rejected as malformed shape;
- recursive descriptor cycle rejected as malformed shape.

### 23.3 Struct decode

Tests must cover:

- empty struct from `MAP_BEGIN MAP_END`;
- flat struct in descriptor field order;
- flat struct in different input field order;
- nested struct;
- exact case-sensitive field matching;
- transient STRING key view accepted without storing key pointer;
- unknown field rejection;
- duplicate field rejection;
- missing field rejection;
- non-string map key rejection;
- wrong root token rejection;
- wrong field value token rejection.

### 23.4 Transaction

Tests must cover:

- non-empty destination fails before reader/provider call;
- malformed shape fails before reader/provider call;
- unsupported shape fails before reader/provider call;
- insufficient depth fails before reader/provider call;
- insufficient scratch fails before reader/provider call;
- failure in second/third field resets earlier written semantic fields;
- nested child failure resets the complete root semantic graph;
- non-semantic layout fields remain untouched on success;
- non-semantic layout fields remain untouched on rollback.

### 23.5 Source behavior

Tests must cover:

- initial reader DONE while scalar expected -> `CBIND_UNEXPECTED_END`;
- initial reader DONE while MAP_BEGIN expected -> `CBIND_UNEXPECTED_END`;
- mid-struct DONE while key/value expected -> `CBIND_UNEXPECTED_END`;
- non-DONE CSerde failure -> `CBIND_SOURCE_ERROR` with exact `source_status`;
- destination rollback after source failure;
- CBind does not issue additional provider calls after the failure it reports.

### 23.6 Scratch/depth exact boundaries

Tests must prove:

- scalar root succeeds with `max_depth=0` and zero scratch;
- root empty struct requires `max_depth>=1` but zero scratch bytes;
- root one-field struct needs one bitmap byte;
- 8 fields need one byte; 9 fields need two bytes;
- nested required scratch equals active-path sum rather than total schema sum;
- sibling nested structs reuse scratch after frame rewind;
- exact required scratch succeeds; one byte less fails before reader consumption.

### 23.7 C++ and dependency boundary

Tests/audits must prove:

- `<cbind/cbind.h>` compiles in C++17;
- C++ test calls and links `cbind_decode`;
- `turbo_cbind` links only CMeta + CSerde;
- production cbind source does not include TurboSTL/Core/CFlow/TurboParser/parser headers;
- no production `malloc/calloc/realloc/free` is introduced in D2.

## 24. CI integration

Existing CMeta/CSerde conformance workflow is extended to trigger on:

```text
cbind/**
```

Selected test regex becomes:

```text
^(cmeta_|cserde_|cbind_|cflow_|turbostl_)
```

Final exact-head acceptance requires fresh Linux and Windows release configure/build/test.

Linux must use the repository presets rather than workflow-local CMake reimplementation.

Windows must continue using the repository MSVC/Ninja preset through `VsDevCmd` as the existing workflow does.

## 25. Acceptance criteria

D2 is complete only when all of the following are true:

1. `TurboUtils::CBind` exists and links only CMeta + CSerde.
2. Public API is context-first.
3. Context/error records use versioned field-end prefix validation.
4. Scalar root decode works with zero scratch and `max_depth=0`.
5. Struct decode uses canonical STRING-key MAP representation.
6. Struct field order is input-independent.
7. Unknown, duplicate, missing, and non-string keys fail under strict D2 semantics.
8. Numeric conversions satisfy the strict range/exactness contract.
9. CBind never silently rounds integer-to-float when the integer is not exactly representable.
10. Supported native scalar storage is limited to provable canonical CMeta built-ins.
11. Unsupported semantic kinds fail before reader consumption.
12. Malformed semantic/storage graphs fail before reader consumption.
13. Destination non-empty state fails before reader consumption.
14. Scratch/depth insufficiency fails before reader consumption.
15. Any post-consumption failure restores all semantic fields to empty.
16. Non-semantic reflected/layout fields remain untouched.
17. Reader is never rewound and failure does not trigger silent remainder skipping.
18. CSerde `DONE` while a token is required becomes `CBIND_UNEXPECTED_END`.
19. Other reader failures become `CBIND_SOURCE_ERROR` with exact CSerde status preserved.
20. Transient STRING key views are not retained.
21. No hidden allocation or fixed field-count limit exists.
22. Recording test support is reused rather than duplicated.
23. Public headers compile/link in C11 and C++17.
24. Exact final head passes fresh Linux and Windows conformance CI.

## 26. Deferred boundaries after D2

The next binding work must build on this kernel rather than widening D2 opportunistically.

Likely follow-on slices are:

```text
D3  owned/borrowed string + bytes storage/lifecycle adapter contract
D4  enum + variant decode
D5  sequence/set/map decode via CMeta construction + Collector
D6  field policy: unknown skip / aliases / optional / defaults
D7  encode core
D8  TurboParser concrete format adapters
```

The exact order may be revised after D2 is implemented and reviewed, but D2 itself must remain the POD scalar + strict struct decode kernel described here.

## 27. Architectural result

After D2, the stable lower stack is:

```text
CMeta semantic data shape
        +
CSerde canonical reader
        |
        v
CBind strict POD decode kernel
        |
        v
native C bool/int/long/size_t/float/double/struct storage
```

The important property is not feature count. It is that CBind begins with a memory-safe, format-neutral, forward-only transaction model that can be extended later without teaching the core about TurboSTL algorithms, parser syntax, concrete string layouts, or process-global policy.