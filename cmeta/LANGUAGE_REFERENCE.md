# CMeta Language Reference

CMeta is a pragmatic modern-C dialect/toolkit implemented on top of strict C11.
It combines a small declaration DSL, finite generic routing, compile-time schema
replay, metadata descriptors, and ordinary C runtime protocols.

CMeta is deliberately **not** a replacement for C++, Rust, or a general-purpose
macro language. New syntax should be added only when it has a concrete C11
implementation and composes cleanly with existing CMeta patterns.

This document is the authoritative language vocabulary. The public surface is
split into four categories so application syntax does not get mixed with
framework generation or runtime APIs.

---

## 1. Application DSL

These declarations are intended for normal application code.

### `Struct(...)`

Declares a C struct and reflection metadata in one statement.

```c
Struct(User,
    (int, id),
    (double, score),
    (const char *, name)
);
```

Field rows are plain `(type, name)` tuples. There is no `Field(...)` wrapper.
The declaration provides the concrete C type plus field metadata helpers such
as:

```c
StructMeta(User);
FieldCount(User);
FieldMeta(User, 0);
FieldFind(User, "score");
```

Use `Struct(...)` when a type benefits from structural metadata. Ordinary C
`struct` remains valid and should be preferred when metadata is unnecessary.

### `Enum(...)`

Declares an enum and immutable reflection metadata.

Auto-valued rows:

```c
Enum(State,
    (READY,   "ready"),
    (RUNNING, "running"),
    (DONE,    "done")
);
```

Explicit stable values:

```c
Enum(HttpStatus,
    (HTTP_OK,        200, "ok"),
    (HTTP_NOT_FOUND, 404, "not_found")
);
```

Rows are either `(symbol, text)` or `(symbol, value, text)`. There is no
`Item(...)` wrapper.

Generated helpers include typed string/symbol conversion and parsing:

```c
State_to_string(value);
State_to_symbol(value);
State_from_string(text, &out);

EnumMeta(State);
EnumString(State, value);
EnumSymbol(State, value);
EnumParse(State, text, &out);
```

### `Traits(...)`

Declares callable type capabilities from tagged rows.

```c
Traits(User,
    (equal, user_equal),
    (hash, user_hash),
    (compare, user_compare),
    (copy, user_copy),
    (move, user_move),
    (destroy, user_destroy)
);
```

Supported callable tags are:

```text
equal
hash
compare
copy
move
destroy
```

CMeta derives both the capability flags and the matching function slots from the
same rows. Duplicate, unknown, or malformed rows are compile-time errors.

`TRIVIAL_COPY` and `TRIVIAL_DESTROY` are descriptor properties, not inferred
callable traits.

### `typed(...)`

The single generic-instantiation entry point.

```c
#include <cmeta/meta.h>

typedef int Key;
typedef int Value;
typedef struct User {
    int id;
} User;

typed(Option, MaybeUser, User);
typed(Pair, Entry, Key, Value);
typed(Tuple, Point3, double, double, double);
```

General form:

```text
typed(kind, generated_name, type_arguments...)
```

CMeta implements **finite generic routing**, not unrestricted templates. The
CMeta aggregate header registers the value kinds `Pair`, `Tuple`, `Option`, and
`Result`. A library may register additional kinds, and `typed(...)` routes each
registered kind to that kind's concrete C11 generator.

Algorithmic container kinds such as `List`, `Vec`, and `HashMap` belong to
TurboSTL. Container users include `<turbostl/typed.h>` and link
`TurboUtils::STL`:

```c
#include <turbostl/typed.h>

typed(List, IntList, int);
typed(Vec, IntVec, int);
typed(HashMap, IntValuesById, int, int);
```

For containers, one `typed(...)` declaration is a complete instantiation. It may
generate the wrapper type, static-inline typed forwarding functions, metadata,
container descriptors, Range factories, and relevant traits. Allocation and
container algorithms remain ordinary compiled C in the underlying library. A
complete facade does not implicitly register its element, key, or value types;
those types must satisfy the finite type-universe rules described below.

Declare each concrete container with its own `typed(...)` statement, as shown
above. There is no batch container declaration syntax.

### `typed_any(...)`

Declares a typed callable together with semantic contract metadata.

```c
typed_any(value, int, increment, (int value)) {
    return value + 1;
}

typed_any(associative, long, add, (long left, long right)) {
    return left + right;
}
```

Form:

```text
typed_any(contract, return_type, name, parameters)
```

Current contract vocabulary:

```text
unknown
value
pure
idempotent
associative
fallible
io
async
stateful
```

Contracts map to the existing CMeta effect/property bitsets. They express
programmer intent and optimization/runtime constraints; they do not introduce a
new execution model.

### `interface(...)`

Declares a small protocol/vtable interface.

```c
#include <cmeta/meta.h>

#define SOURCE_METHODS(X, I) \
    X(I, R1, bool, next, int *, out) \
    X(I, V0, void, close, _)

interface(Source, SOURCE_METHODS);
```

Method row kinds are:

```text
R0 R1 R2 R3 R4   non-void return, 0..4 arguments after self
V0 V1 V2 V3 V4   void return,     0..4 arguments after self
```

An interface value is conceptually `{ self, vtable }` plus implementation and
capability metadata. This is a protocol mechanism, not a class hierarchy.

### `implements(...)`

Binds an ordinary C implementation to an interface.

```c
implements(Source,
           file_source,
           SOURCE_CAN_SEEK,
           .next = file_source_next,
           .close = file_source_close);
```

`implements(...)` only means "implements this interface/protocol". It is not a
container generation phase.

The natural names are conditional aliases. If
`CMETA_NO_NATURAL_INTERFACE_NAMES` is defined, or a host header has already
claimed `interface` or `implements`, use the collision-safe spellings:

```c
CMETA_INTERFACE(Source, SOURCE_METHODS);

CMETA_IMPLEMENTS(Source,
                 file_source,
                 SOURCE_CAN_SEEK,
                 .next = file_source_next,
                 .close = file_source_close);
```

Framework and public headers should prefer `CMETA_INTERFACE(...)` and
`CMETA_IMPLEMENTS(...)`; application-local code may use the natural aliases when
the host environment leaves them available.

---

## 2. Framework DSL

These forms are primarily for libraries and CMeta/CFlow internals.

### `Schema(...)`

Defines or expands parenthesized row data through a mapper.

```c
#define MyRows(M) \
    Schema(M,
        (ROW_A, 1),
        (ROW_B, 2))
```

`Schema(...)` owns row unpacking. It is a finite C-preprocessor code-generation
kernel, not a runtime data structure. The tuple-list kernel accepts at most 16
rows per invocation. `Struct(...)`, `Enum(...)`, `Traits(...)`, `Schema(...)`,
and `Operators(...)` share that row-count limit; `Tuple` accepts 2 through 16
type arguments. Split larger declarations into separate stable concepts instead
of depending on an implementation-specific macro expansion failure.

### `Replay(...)`

Applies a named schema/producer to a consumer mapper.

```c
#define DECLARE(name, value) int name = value;
Replay(MyRows, DECLARE)
```

Conceptually:

```text
Schema = source rows / compile-time representation
Replay = consumer application
```

One schema can therefore feed declaration, metadata, validation, counting, or
other framework mappers without creating another user-facing language layer.

### `Operators(...)`

CFlow's specialized operator-schema normalizer.

Structured source rows group call, function, flow, semantic, and effect
information, then normalize to the established flat consumer ABI before replay.
This keeps source syntax readable without inventing a second operator semantics.

`Operators(...)` is framework vocabulary. Application code normally consumes the
higher-level CFlow API instead of authoring raw operator schemas.

---

## 3. Runtime Protocol

The following are normal C APIs/protocols, not additional language keywords.

### Type metadata and identity

Core concepts include:

```text
cmeta_type_desc
cmeta_type_traits
cmeta_type_identity
CMETA_TYPEOF(T)
```

Descriptors carry semantic type information such as name, size, alignment,
kind, pointee, traits, and identity. Header-generated descriptor addresses are
not required to be process-global type IDs; consumers compare semantic type
identity/content.

`CMETA_TYPEOF(T)` is a finite `_Generic` lookup, not reflection over arbitrary C
types. `T` must be compatible with exactly one C type named by the active
`CMETA_TYPE_LIST`; by default that aliases `CMETA_CALLABLE_TYPE_LIST`, which in
turn defaults to `CMETA_KNOWN_TYPE_LIST`. A `typedef` alias reuses its underlying
compatible type's descriptor. Do not register a second row for such an alias,
because two compatible `_Generic` associations are a compile-time error.

Applications normally extend the defaults through `CMETA_USER_TYPE_LIST` in a
shared configuration header included before the first CMeta header in every
affected translation unit. Overrides of `CMETA_KNOWN_TYPE_LIST` and
`CMETA_CALLABLE_TYPE_LIST` follow the same rule. Changing the callable list
changes the ABI of `cmeta_sig` and `cmeta_callable`, so the list must match in
every translation unit and in the linked CMeta library build. Every custom row
must name descriptors and traits that the program defines with the ownership
operations required by its consumers.

The built-in type rows and finite callable relation graph are declared in
`formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/BuiltinSignatures.lean`.
Validation rejects empty categories, duplicates, and relations that reference
unknown built-in type tokens before rendering. The checked-in
`cmeta/generated/builtin_signature_manifest.h` preserves the public macro and
signature order, is consumed by normal C/C++ compilation without a Lean
dependency, and must not be edited manually. From the formal package, use:

```text
lake exe cmeta-signature-gen --write ../../cmeta/include/cmeta/generated/builtin_signature_manifest.h
lake exe cmeta-signature-gen --check ../../cmeta/include/cmeta/generated/builtin_signature_manifest.h
```

This generation boundary owns only built-ins. Application rows and
`CMETA_USER_UNARY_RELATION_LIST`, `CMETA_USER_BINARY_RELATION_LIST`, and
`CMETA_USER_GENERATOR_RELATION_LIST` remain shared compile-time configuration;
all translation units must still see the same callable ABI.

`Struct(T, ...)` and `Traits(T, ...)` generate structural and callable metadata,
but do not add `T` to either finite type universe. `CMETA_TYPEOF(T)` returns
`NULL` when no compatible registered type exists. APIs that require a descriptor
then fail according to their own contract: a container range lookup can return
`false`, range traversal can return `CMETA_GEN_ERROR`, and initialization or
collector boundaries can return invalid-argument or type-mismatch errors. Check
the exact API result; there is no implicit fallback.

### Callable protocol

Core runtime values include:

```text
cmeta_fn
cmeta_callable
cmeta_sig_desc
```

They provide typed signature metadata, effect/property contracts, erased invoke
or generator entry points, and optional inline captures.

### Range protocol

`cmeta_range` is an allocation-free borrowed traversal protocol. Range capability
flags include:

```text
SIZED
ORDERED
SORTED
UNIQUE
CONTIGUOUS
RANDOM_ACCESS
REUSABLE
```

Typed containers may expose default, key, value, or entry ranges through their
descriptors.

A Range borrows both its source object and its element descriptor.
The source handle storage must outlive the Range and every cursor used with it.
Creating a Range does not allocate, retain, or extend either lifetime. Unless a
concrete container promises otherwise, append, erase, resize, reset, destroy,
slot reuse, and undocumented cross-thread access invalidate the traversal.

When a Range supplies version tracking, `cmeta_range_next(...)` reports
`CMETA_GEN_MUTATED` before changing the cursor or output, but only while the
source handle storage remains alive. Version tracking cannot make expired or
freed storage safe, and a Range without a version callback cannot detect
mutation generically.

### Collector protocol

`cmeta_collector` is a bounded transactional protocol for constructing a
caller-owned output from typed borrowed values.

Stable states:

```text
ZERO
BEGUN
ACCEPTING
COMMITTED
ABORTED
```

The facade performs validation and exactly-once abort behavior but does not own a
general allocator, scheduler, retry system, or synchronization policy.

### Container descriptor protocol

`cmeta_container_desc` describes the runtime capabilities of a typed container,
including type metadata, Range factories, and an optional collector factory.
The raw algorithms remain ordinary C implementation code.

### Interface runtime values

`interface(...)` generates an ordinary C `{ self, vtable }` protocol value,
inline forwarding functions, capability metadata, and reflection metadata.

---

## 4. Reserved future syntax

The following names describe useful directions but are **not current language
features and are not compatibility promises**:

```text
Lambda
Bind
Variant
Match
Array
SmallVec
RingBuffer
```

Related ideas may already exist in Lean models, CFlow internals, experiments, or
ordinary C implementations. They become CMeta syntax only after a concrete,
useful, maintainable C11 implementation exists.

The preferred evolution rule is:

1. solve the concrete use case with ordinary C and existing CMeta patterns;
2. identify repeated boilerplate or a stable semantic pattern;
3. add the smallest composable CMeta abstraction;
4. validate it on GCC, Clang, and MSVC portability lanes;
5. add formal proof only when the new feature needs a property not already
   covered by existing evidence.

CMeta does not need to "complete" a universal language design before it is
useful.

---

## Removed syntax

### `Containers(...)`

Removed. Do not keep or reintroduce it as an alias.

Old style:

```c
/* removed */
Containers(
    (List, IntList, int),
    (Vec, IntVec, int)
);
```

Current style:

```c
#include <turbostl/typed.h>

typed(List, IntList, int);
typed(Vec, IntVec, int);
```

The explicit form keeps one generic entry point and avoids a second batch DSL
that adds no semantic capability.

### Container `implement(...)`

The earlier declaration/implementation split is also removed. Typed container
instantiation is single-stage.

---

## Design rule of thumb

Use the smallest layer that solves the problem:

```text
ordinary C
    ↓ when repetitive metadata/code generation appears
Application DSL
    ↓ when a library needs reusable compile-time row generation
Framework DSL
    ↓ for execution/storage/interoperation
Runtime Protocol
```

Do not add a new keyword when an existing declaration, schema mapper, descriptor,
interface, or ordinary C function composes cleanly enough.
