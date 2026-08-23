# CMeta capability catalog — v50

CMeta is a pragmatic modern-C dialect/toolkit over strict C11. It is finite,
schema-driven, and intentionally compositional rather than a universal language.

## Implemented core

- strict-C11 preprocessor kernel: finite `FOR_EACH`, indexed replay, repeat;
- unified tuple schema kernel through `Schema(...)` / `Replay(...)`;
- `Struct(...)` single-declaration struct + field metadata;
- `Enum(...)` single-declaration enum + immutable metadata;
- tagged-row `Traits(...)` with duplicate/unknown tag rejection;
- finite generic routing through the single `typed(kind, ...)` entry point;
- CMeta value kinds: `Pair`, `Tuple`, `Option`, `Result`;
- `typed_any(...)` first-class callable declarations with semantic contracts;
- `interface(...)` / `implements(...)` protocol/vtable declarations;
- known-type and callable-type universe separation;
- semantic type identity for atoms, pointers, const forms, and generic applications;
- allocation-free `cmeta_range` runtime protocol;
- transactional bounded `cmeta_collector` runtime protocol;
- type/signature registry, callable substrate, effect/property metadata, and
  inline captures.

## Application DSL

Canonical application syntax is:

```text
Struct(...)
Enum(...)
Traits(...)
typed(...)
typed_any(...)
interface(...)
implements(...)
```

`Traits(...)` accepts tagged rows only:

```c
Traits(User,
    (equal, user_equal),
    (hash, user_hash),
    (compare, user_compare),
    (copy, user_copy),
    (move, user_move),
    (destroy, user_destroy));
```

The historical positional `Traits(name, flags, ...)` form and
`CMETA_TRAITS_POSITIONAL` constructor are removed.

## Framework DSL

Framework-level tuple schemas use:

```c
#define MySchema(M) Schema(M, (ROW_A, ...), (ROW_B, ...))
Replay(MySchema, mapper)
```

CFlow operator declarations use structured `Operators(...)` rows grouped by
call, function, flow, semantic, and effect metadata. `Operators(...)` normalizes
those rows back to the established flat consumer ABI.

## Generic value types

Header-complete CMeta value kinds include:

```text
Pair            two type arguments; first / second
Tuple           2..16 type arguments; positional v0..v15
Option          one type argument; has_value + value
Result          two type arguments; ok + value/error union
```

Canonical forms:

```c
typed(Pair, Entry, Key, Value);
typed(Tuple, Coordinate, double, double, double);
typed(Option, MaybeUser, User);
typed(Result, LoadResult, User, Error);
```

## Containers and turbostl

Algorithmic containers are owned by `turbostl`, not by the CMeta aggregate
header. `turbostl` may register finite generic kinds such as:

```text
Vec        Deque      List
Stack      Queue      Heap
Set        HashSet
HashMap    Map        MultiMap
BTree      BPlusTree
```

They still use CMeta's common `typed(...)`, descriptor, Range, collector, and
traits protocols. CMeta itself does not expose a `Containers(...)` batch DSL or
a container `implement(...)` generation phase.

## Type identity and finite universes

CMeta separates:

```text
CMETA_KNOWN_TYPE_LIST      descriptors / reflection / type identity
CMETA_CALLABLE_TYPE_LIST   generated callable signature families
```

A legacy project defining only `CMETA_TYPE_LIST` retains the old behavior: the
same list is used for both universes.

`cmeta_type_desc` may carry a `cmeta_type_identity`. Built-in atoms and pointer
descriptors have stable identities; generic applications can identify a finite
constructor plus semantic argument identities. Consumers compare semantic
identity rather than descriptor addresses.

Generated descriptor objects may be translation-unit local. Their addresses are
not stable program-wide type IDs.

## Runtime protocols

### Range

`cmeta_range` is an allocation-free borrowed traversal protocol. Capability
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

### Collector

`cmeta_collector` is a bounded single-threaded transaction over borrowed typed
values. Stable states are:

```text
ZERO
BEGUN
ACCEPTING
COMMITTED
ABORTED
```

The façade validates input and type compatibility, preserves the first error,
and performs exactly-once abort behavior. It does not own allocation, retry,
I/O, scheduling, or synchronization policy.

### Callable

`cmeta_fn`, `cmeta_callable`, and `cmeta_sig_desc` form the runtime callable
protocol. `typed_any(...)` attaches effect/property contracts without creating a
new execution engine.

`cmeta_callable.dispatch` is an explicit dispatch contract. Generated named
`typed_any(...)` callables advertise `CMETA_CALLABLE_DISPATCH_CANONICAL_RAW`;
their active `cmeta_fn` target may be batch-decoded by a consumer after a
successful bind. `CMETA_CALLABLE_INIT` and zero initialization default to
`CMETA_CALLABLE_DISPATCH_ADAPTER`, so custom adapters and inline captures remain
authoritative even when capture size is zero. Canonical raw callables must have
zero capture and a valid active raw target. Adapter callables may intentionally
omit a raw target when their protocol-specific invoke/generate adapter implements
the operation.

### Interface

`interface(...)` produces a conventional `{ self, vtable }` protocol value plus
capability and reflection metadata. It is not a class hierarchy.

## Removed syntax

```text
Containers(...)
positional Traits(name, flags, ...)
container implement(...)
DeclareContainers(...)
ImplementContainers(...)
```

Do not preserve these as compatibility aliases.

## Reserved future syntax

Examples currently reserved, not implemented or promised:

```text
Lambda
Bind
Variant
Match
Array
SmallVec
RingBuffer
```

New syntax should be added only after an ordinary-C implementation demonstrates
a repeated, stable pattern that the new abstraction materially simplifies.
