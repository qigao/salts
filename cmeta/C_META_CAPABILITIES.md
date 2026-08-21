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
- independent structural TypeId core with `ATOM`, `POINTER`, `CONST`, and finite generic `APPLY` forms;
- finite `GenericConstructor` metadata with arity validation;
- explicit `KnownTypes` versus `CallableSignatures` universe separation;
- typed callable substrate;
- contract/effect/property metadata;
- first-class typed callable values and inline captures;
- `Enum(...)` single-declaration enum + metadata;
- `Struct(...)` single-declaration struct + field metadata;
- `interface(...)` / `implements(...)` interface protocol;
- finite generic kind dispatcher behind `typed(kind, ...)`;
- allocation-free `cmeta_range` protocol and range traits.

The TypeId implementation is intentionally independent of the current `cmeta_type_desc` layout. Descriptor-to-TypeId integration is a separate migration step.

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

The semantic foundation for future compositional application is now implemented as:

```text
GenericConstructor + TypeId + Apply(Constructor,[TypeId...])
```

The source spelling `M<A,B>` remains a future CMeta Extend feature.

## Structural type identity

`cmeta_type_identity` represents semantic type structure independently from aliases, generated symbols and translation-unit-local descriptor addresses.

The implemented forms are:

```text
ATOM(stable-id)
POINTER(base)
CONST(base)
APPLY(constructor, args...)
```

Generic constructor equality is based on stable semantic constructor ID, not descriptor object address. Application equality recursively preserves argument order.

Real C witnesses generate a Lean snapshot, and Lean recomputes the same structural equalities in `CMeta.TypeIdentityConformance`.

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

The new TypeId core supplies the structural identity model, but current descriptor APIs have not yet been bridged to it.

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

## Known types and callable signatures

The configuration layer now distinguishes:

```text
CMETA_KNOWN_TYPE_LIST
CMETA_CALLABLE_TYPE_LIST
```

`CMETA_TYPE_LIST` remains a compatibility alias during migration.

Descriptor declaration/definition and the type registry consume the known-type universe. Full/balanced callable Cartesian generation consumes the callable-type universe.

A dedicated compile probe adds a known-only type while retaining the five builtin callable types and verifies that the full signature count remains unchanged. This prevents reflected/generic types from automatically inflating the N²/N³ callable products.

The longer-term callable direction is still exact demand-driven signatures rather than broad Cartesian products.

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

## Formal verification status

The TypeId slice is connected to CI through:

```text
real C TypeId implementation
  -> cmeta_type_identity_conformance_gen
  -> TypeIdentityGeneratedC.lean
  -> CMeta.TypeIdentityConformance
  -> lake build --wfail
```

CI also builds `cmeta_type_universe_probe` to lock the KnownTypes/CallableSignatures separation.

The proof-placeholder guard continues to reject `axiom`, `constant`, `sorry`, and `admit` in formal modules.

## Approved architecture direction — not yet implementation claims

### Descriptor bridge and canonical strict-C11 application

Still planned:

- bridge `cmeta_type_desc` to structural TypeId without breaking existing descriptor initialization/source compatibility;
- canonical strict-C11 generic instantiation so logically identical applications can share one concrete C representation;
- structured reflection of generic constructor/arguments through descriptor APIs.

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

First-party module for finite table-driven state machines using Core `CType`/TypeId, `Callable`, effects/properties and finite graph analysis.

### CMeta Exec

First-party module for `Task<T>`, `Resumable`, `Waitable`, `Waker`, `Executor`, cancellation, scopes and coordination. Coroutine and OS support remain replaceable adapters.

## Natural next implementation work

Architecture-approved but still requiring focused implementation specs/plans:

- descriptor ↔ TypeId bridge and semantic descriptor equality migration;
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
