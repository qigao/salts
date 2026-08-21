# CMeta capability catalog — v50+

CMeta is a finite, schema-driven typed metaprogramming substrate for strict C11.

This document distinguishes **implemented capabilities** from the **approved architecture direction**. Architectural designs are not listed as implemented until production code and tests exist.

Architecture references:

- `../docs/superpowers/specs/2026-08-21-cmeta-hexagonal-architecture-design.md`
- `../docs/superpowers/specs/2026-08-21-cmeta-type-application-design.md`
- `../docs/superpowers/specs/2026-08-21-cmeta-state-exec-concurrency-design.md`

## Architecture boundary

```text
CMeta Core       semantic hexagonal kernel
CMeta Extend     optional source syntax / lowering frontend
State / Exec     first-party modules built on Core
CFlow            typed value-flow consumer
minicoro / OS    infrastructure adapters
```

The current shipped/implemented CMeta code is primarily the strict-C11 Core substrate and first-party value/container facilities. `M<A,B>` source syntax, `cmc`, CMeta State and CMeta Exec are architecture work, not yet claimed as implemented by this catalog.

## Implemented Core substrate

- strict-C11 preprocessor kernel: finite `FOR_EACH`, indexed replay, repeat;
- unified tuple schema kernel through `Schema(...)` / `Replay(...)`;
- finite type/signature registry;
- typed callable substrate;
- contract/effect/property metadata;
- first-class typed callable values and inline captures;
- `Enum(...)` single-declaration enum + metadata;
- `Struct(...)` single-declaration struct + field metadata;
- `interface(...)` / `implements(...)` interface protocol;
- finite generic kind dispatcher behind `typed(kind, ...)`;
- allocation-free `cmeta_range` protocol and range traits.

## Implemented first-party value/container facilities

- `Pair`, `Tuple`, `Option`, `Result` strict-C11 generic kinds;
- header-complete typed-container instantiation;
- direct batch container instantiation through `Containers(...)`;
- one-pointer typed-container object headers plus static `cmeta_container_desc` metadata;
- erased default/key/value/entry Range factories;
- typed container integration for the Turbo container algorithms listed below.

## Schema / Replay

Framework-level tuple schemas use:

```c
#define MySchema(M) Schema(M, (ROW_A, ...), (ROW_B, ...))
Replay(MySchema, mapper)
```

`Enum`, `Struct`, and operator-generation infrastructure reuse the common row kernel. `Schema/Replay` is primarily framework-author infrastructure.

Application `Containers(...)` is deliberately a different semantic surface: it directly instantiates all listed concrete types and does not require naming a schema for later implementation replay.

## Generic value types

Header-complete strict-C11 value kinds include:

```text
Pair<T,U>       first / second
Tuple<T...>     positional v0..v15, arity 2..16
Option<T>       has_value + value
Result<T,E>     ok + value/error union
```

Current canonical strict-C11 forms are explicit named instantiations:

```c
typed(Pair, Entry, Key, Value);
typed(Tuple, Coordinate, double, double, double);
typed(Option, MaybeUser, User);
typed(Result, LoadResult, User, Error);
```

The approved architecture adds a future compositional semantic type-application model while preserving these C11 forms during migration.

## Single-stage typed containers

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

The declaration derives:

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

Generated descriptor objects may also be TU-local. Their addresses are not a stable program-wide type ID. CMeta type equality and generic consumers must use semantic descriptor/type identity instead of assuming pointer equality across translation units.

An object initialized in one translation unit may store a descriptor pointer emitted by that TU; erased consumers in another TU can safely use semantic descriptor data while the originating code remains linked.

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

For element types outside CFlow's finite callable universe, generated facades can still provide local object descriptors so Range remains independently usable. Typed CFlow callback signatures currently require types to participate in the configured finite callable universe.

## Callable universe constraint

The current signature system derives finite callable families from the configured type list. This is practical for the current small universe but does not scale directly to arbitrary compositional generic applications.

The approved architecture therefore distinguishes:

```text
KnownTypes
CallableSignatures
```

A future implementation must not automatically add every known generic application to every callable Cartesian-product family.

## Public API boundary today

Current application-facing strict-C11 declarations are primarily:

```text
Struct(...)
Enum(...)
typed(...)
Containers(...)
```

plus runtime capability APIs supplied by consumer modules such as CFlow.

`Schema/Replay` is framework-generation infrastructure. `implements(...)` is an interface/protocol declaration and is unrelated to the removed container implementation phase.

## Approved architecture direction — not yet implementation claims

### Core type identity and finite type application

Planned semantic direction:

```text
GenericConstructor + TypeId + Apply(Constructor,[TypeId...])
```

with structural identity independent of aliases, display strings, descriptor addresses and generated C symbol names.

### CMeta Extend

Optional frontend/lowering layer for source forms such as:

```text
M<A,B>
type aliases
machine syntax
match syntax
future async/await syntax
```

Extend is an adapter and must lower to existing Core/module semantics.

### CMeta State

First-party module for finite table-driven state machines using Core `CType`, `Callable`, effects/properties and finite graph analysis.

### CMeta Exec

First-party module for `Task<T>`, `Resumable`, `Waitable`, `Waker`, `Executor`, cancellation, scopes and coordination. Coroutine and OS support remain replaceable adapters.

## Natural next implementation work

Architecture-approved but still requiring focused implementation specs/plans:

- Core structured TypeId and semantic type equality;
- canonical strict-C11 generic application backend;
- demand-driven exact callable signature generation;
- comparator/hash/copy/move/destroy trait registration;
- CMeta Extend frontend (`cmc`) and `M<A,B>` lowering;
- CMeta State Core;
- CMeta Exec Core;
- minicoro coroutine adapter;
- native executor adapters;
- Pair/Tuple/Option/Result structural reflection descriptors;
- variant/sum types and matching after the type model is stable;
- serde/RPC generation as consumers rather than Core expansion.

Implementation status must be updated here only after code, tests and relevant conformance evidence exist.
