# CMeta capability catalog — v50

CMeta is a finite, schema-driven compile-time metadata/code-generation layer for strict C11 and a pragmatic toolkit for building a modern C dialect. The authoritative syntax boundary is [`LANGUAGE_REFERENCE.md`](LANGUAGE_REFERENCE.md).

## Implemented core

- strict-C11 preprocessor kernel: finite `FOR_EACH`, indexed replay, repeat;
- unified tuple schema kernel through `Schema(...)` / `Replay(...)`;
- specialized operator schema normalization through `Operators(...)`;
- type/signature registry and typed callable substrate;
- contract/effect/property metadata and `typed_any(...)` declarations;
- first-class typed callable values and inline captures;
- `Enum(...)` single-declaration enum + metadata;
- `Struct(...)` single-declaration struct + field metadata;
- tagged-row `Traits(...)` declarations deriving callable trait flags and slots;
- `interface(...)` / `implements(...)` interface protocol;
- finite generic kind dispatcher behind `typed(kind, ...)`;
- header-only complete typed-container instantiation through `typed(...)`;
- allocation-free `cmeta_range` protocol and range traits;
- one-pointer typed-container object headers plus static `cmeta_container_desc` metadata;
- erased default/key/value/entry Range factories;
- explicit type traits for equality, hash, comparison, copy, move, and
  destruction, attached to semantic type descriptors;
- transactional bounded `cmeta_collector` façade with semantic type admission,
  first-error preservation, and exactly-once adapter abort;
- optional value-oriented collector factory in `cmeta_container_desc`.

## Application DSL

The stable application-facing declaration vocabulary is:

```text
Struct(...)
Enum(...)
Traits(...)
typed(...)
typed_any(...)
interface(...)
implements(...)
```

Application generic declarations intentionally have one entry point: `typed(kind, ...)`. Multiple concrete container types are declared with multiple `typed(...)` statements rather than a separate batch-container DSL.

## Framework DSL

Framework-level tuple schemas use:

```c
#define MySchema(M) Schema(M, (ROW_A, ...), (ROW_B, ...))
Replay(MySchema, mapper)
```

`Enum`, `Struct`, and operator-generation infrastructure reuse the common row kernel. `Schema/Replay` is primarily framework-author infrastructure.

CFlow operator declarations use structured source rows through `Operators(...)` but normalize to the existing flat consumer ABI before `Replay(CFlowOperators, mapper)` invokes a consumer. The structured grouping is therefore a source-level representation change, not a second operator semantics.

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

## Structured type traits

Callable type capabilities are declared once with tagged rows:

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

CMeta derives `CMETA_TRAIT_EQUAL`, `HASH`, `COMPARE`, `COPY`, `MOVE`, and `DESTROY` flags from the rows and initializes the matching function slots. Duplicate, unknown, and malformed rows are compile-time errors. `CMETA_TRAIT_TRIVIAL_COPY` and `CMETA_TRAIT_TRIVIAL_DESTROY` remain explicit descriptor properties rather than inferred function capabilities.

## Single-stage typed containers — v50

A concrete algorithmic container is fully instantiated by one declaration:

```c
typed(List, IntList, int);
```

Multiple instantiations remain explicit and uniform:

```c
typed(List, IntList, int);
typed(HashMap, Table, int, long);
typed(BTree, Tree, int, long, int_compare);
```

There is no public container batch declaration, `implement(...)`, `DeclareContainers(...)`, or `ImplementContainers(...)` phase.

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

## Runtime protocol boundary

Type descriptors/identity, callables, ranges, collectors, container descriptors, and generated interface values are ordinary C runtime/meta APIs. They are capabilities, not additional DSL keywords.

## Removed syntax

`Containers(...)` is removed and must not be kept as a compatibility alias. Write one `typed(...)` declaration per generated type.

The earlier container `implement(...)` / declaration-then-implementation generation model is also removed.

## Reserved future directions

The following are ideas, not current language promises:

```text
Lambda / Bind
Variant / Match
Array / SmallVec / RingBuffer
```

Other natural extensions include kind-level `_Generic` ergonomic APIs, additional opt-in inference over registered traits, richer reflection descriptors, ownership-aware destruction/serde generation, and additional container/range operations.

New syntax should appear only after an implementable C11 pattern proves useful. CMeta does not need a complete universal language design before features can be composed and shipped.
