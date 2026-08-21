# CMeta capability catalog — v50

CMeta is a finite, schema-driven compile-time metadata/code-generation layer for strict C11.

## Implemented core

- strict-C11 preprocessor kernel: finite `FOR_EACH`, indexed replay, repeat;
- unified tuple schema kernel through `Schema(...)` / `Replay(...)`;
- type/signature registry and typed callable substrate;
- contract/effect/property metadata;
- first-class typed callable values and inline captures;
- `Enum(...)` single-declaration enum + metadata;
- `Struct(...)` single-declaration struct + field metadata;
- `interface(...)` / `implements(...)` interface protocol;
- finite generic kind dispatcher behind `typed(kind, ...)`;
- header-only complete typed-container instantiation;
- direct batch container instantiation through `Containers(...)`;
- allocation-free `cmeta_range` protocol and range traits;
- one-pointer typed-container object headers plus static `cmeta_container_desc` metadata;
- erased default/key/value/entry Range factories.
- explicit type traits for equality, hash, comparison, copy, move, and
  destruction, attached to semantic type descriptors;
- transactional bounded `cmeta_collector` façade with semantic type admission,
  first-error preservation, and exactly-once adapter abort;
- optional value-oriented collector factory in `cmeta_container_desc`.

## Schema / Replay

Framework-level tuple schemas use:

```c
#define MySchema(M) Schema(M, (ROW_A, ...), (ROW_B, ...))
Replay(MySchema, mapper)
```

`Enum`, `Struct`, and operator-generation infrastructure reuse the common row kernel. `Schema/Replay` is primarily framework-author infrastructure.

In v50, application `Containers(...)` is deliberately a different semantic surface: it directly instantiates all listed concrete types and does not require naming a schema for later implementation replay.

## Generic value types

Header-complete value types include:

```text
Pair<T,U>       first / second
Tuple<T...>     positional v0..v15, arity 2..16
Option<T>       has_value + value
Result<T,E>     ok + value/error union
```

Canonical forms:

```c
typed(Pair, Entry, Key, Value);
typed(Tuple, Coordinate, double, double, double);
typed(Option, MaybeUser, User);
typed(Result, LoadResult, User, Error);
```

## Single-stage typed containers — v50

A concrete algorithmic container is fully instantiated by one declaration:

```c
typed(List, IntList, int);
```

or in a direct batch:

```c
Containers(
    (List, IntList, int),
    (HashMap, Table, int, long),
    (BTree, Tree, int, long, int_compare)
);
```

There is no public container `implement(...)`, `DeclareContainers(...)`, or `ImplementContainers(...)` phase.

The declaration immediately derives:

```text
concrete wrapper type
static-inline typed forwarding functions
type/element/key/value metadata
container descriptor
Range factory/factories
container/range traits
```

Raw allocation, growth, hashing, queue/deque logic, heapification, B-tree insertion/deletion/balancing, and B+ tree behavior remain ordinary C algorithms in the underlying container library.

## Multi-translation-unit semantics

Generated typed forwarding code is header-local `static inline`, so the same declaration header may be included by many translation units without requiring one implementation TU.

Generated descriptor objects are also header-local. Their addresses are not a stable program-wide type ID. CMeta type equality and generic consumers must use semantic descriptor/type identity instead of assuming descriptor pointer equality across translation units.

An object initialized in one translation unit may store a descriptor pointer emitted by that TU; erased consumers in another TU can safely call through that descriptor while the originating code remains linked into the program.

## Typed container kinds

Current Turbo integration covers:

```text
Vec        Deque      List
Stack      Queue      Heap
Set        HashSet
HashMap    Map        MultiMap
BTree      BPlusTree
```

## Range + descriptor capability

Current Range views include:

```text
Vec / Deque / List / Stack / Queue / Heap      -> default element Range
Set / HashSet                                  -> default key Range
HashMap / Map / BTree / BPlusTree              -> keys/values/entries Range views
```

Ranges carry an element descriptor plus flags such as `SIZED`, `ORDERED`, `SORTED`, `UNIQUE`, `CONTIGUOUS`, `RANDOM_ACCESS`, and `REUSABLE`.

## Transactional collector capability

`cmeta_collector` provides a single-threaded transaction over borrowed typed
values. Its only successful terminal transition is `BEGUN`/`ACCEPTING` to
`COMMITTED`; adapter failure, input/type validation failure, or a hard-capacity
rejection transitions to `ABORTED` and invokes adapter cleanup exactly once.
The façade validates `count >= limit` before dispatch, never grows or retries,
and maps unknown non-OK callback statuses to `CMETA_CALLBACK_ERROR`.

`cmeta_container_desc::collector` is optional. When present, it creates a
value-oriented collector from caller-owned zero output and a maximum element
count; `NULL` means that concrete container does not support collection.

For element types outside CFlow's finite callable universe, generated facades can still provide local object descriptors so Range remains independently usable. Typed CFlow callback signatures require the type to be registered in the CMeta/CFlow type universe.

## Public API boundary

Application-facing declarations should generally be limited to:

```text
Struct(...)
Enum(...)
typed(...)
Containers(...)
```

and runtime capability APIs such as `stream(...)`.

`Schema/Replay` is for library/framework generation. `implements(...)` is an interface/protocol declaration and remains valid; it is unrelated to the removed container implementation phase.

## Next natural extensions

- kind-level `_Generic` ergonomic APIs such as `list_push(&users, value)`;
- additional opt-in inference built on explicitly registered traits;
- Array / SmallVec / RingBuffer finite generic kinds;
- variant/sum types and richer pattern matching;
- Pair/Tuple/Option/Result reflection descriptors;
- ownership-aware destruction and serde generation;
- standard in-place B-tree deletion and bounded/range-query iterators.
