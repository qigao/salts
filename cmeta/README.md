# CMeta v50+

CMeta is a finite, schema-driven typed metaprogramming substrate for strict C11.

Its architecture is intentionally split into:

```text
CMeta Core       stable semantic hexagonal kernel
CMeta Extend     optional source-language frontend/lowering layer
State / Exec     first-party modules built on Core
CFlow            typed value-flow consumer
minicoro / OS    replaceable infrastructure adapters
```

CMeta does not attempt to reproduce unrestricted C++ templates, Zig `comptime`, Rust ownership semantics, or a second runtime language.

The architecture baseline is documented in:

- `../docs/superpowers/specs/2026-08-21-cmeta-hexagonal-architecture-design.md`
- `../docs/superpowers/specs/2026-08-21-cmeta-type-application-design.md`
- `../docs/superpowers/specs/2026-08-21-cmeta-state-exec-concurrency-design.md`

## Core principle

CMeta Core owns semantic meaning. CMeta Extend may improve spelling and diagnostics, but it must lower to Core/module APIs.

A useful test is:

> Removing CMeta Extend must remove convenience syntax, not semantic capability.

Therefore strict C11 remains a complete supported path.

## Public strict-C11 programming model

Application users currently use semantic DSLs such as:

```c
Struct(User,
    (int, id),
    (double, score)
);

Enum(State,
    (READY, "ready"),
    (DONE,  "done")
);

typed(Option, MaybeUser, User);
typed(List, UserList, User);

Containers(
    (Vec, UserVec, User),
    (HashMap, UserMap, int, User)
);
```

For typed containers, declaration is complete instantiation. There is no public container `implement(...)` phase.

These APIs are Core/module spellings and do not require a parser or code-generation frontend.

## CMeta Extend

CMeta Extend is an optional future/ongoing frontend that may provide source forms such as:

```c
Result<User, Error>
Task<Result<User, Error>>
type UserResult = Result<User, Error>;
machine Connection { ... }
async fn load(...) { ... }
```

These forms are syntax adapters only. For example:

```text
Result<User,Error>
       ↓ Extend parse/lowering
Core GenericConstructor(Result) + TypeIds(User,Error)
       ↓
explicit generated CMeta/Core declarations
       ↓
ordinary C11
```

The frontend must not duplicate or replace Core type equality, trait semantics, callable contracts, Task lifecycle semantics, or machine validity rules.

## Unified Schema / Replay kernel

Framework authors can define finite row schemas with:

```c
#define MyRows(M) \
    Schema(M, \
        (ROW_A, 1), \
        (ROW_B, 2))

Replay(MyRows, SOME_MAPPER)
```

`Schema` owns parenthesized-row unpacking. `Replay` applies a named schema to a mapper. `Enum`, `Struct`, and CFlow operator metadata reuse this kernel internally.

Application `Containers(...)` is intentionally not a named-schema alias; it directly instantiates each row.

## Finite generic routing

Libraries register finite generic kinds such as `Option`, `Result`, `List`, `Vec` or `HashMap`.

The strict-C11 entry point:

```c
typed(kind, ...)
```

routes a registered kind to `CMETA_TYPED_<Kind>`. Unregistered kinds may fall through to a framework-provided typed fallback; CFlow uses that path for lowercase operator callables such as `typed(map, ...)`.

Generic constructors are finite factories, not user-programmable compile-time templates.

## Type application direction

The semantic model is moving toward compositional finite type application:

```text
Apply(Constructor, [TypeId...]) -> TypeId
```

CMeta Extend may spell this as:

```c
Task<Result<User, Error>>
Map<String, List<User>>
```

while strict C11 continues to use explicit named instantiation.

Semantic type identity must be independent from aliases, display names, generated C symbols and translation-unit-local descriptor addresses.

## Known types and callable signatures

A reflected/known CMeta type is not automatically part of every callable signature family.

CMeta distinguishes conceptually:

```text
KnownTypes
CallableSignatures
```

This prevents compositional generic types from causing uncontrolled Cartesian-product growth in the typed callable universe.

Callable signatures should evolve toward explicit finite demand while preserving Core `Callable`, effect/property and contract semantics.

## Single-stage typed containers

A declaration such as:

```c
typed(List, UserList, User);
```

expands a complete typed facade containing:

- the concrete wrapper type;
- header-local `static inline` forwarding functions;
- element/key/value type metadata;
- one static container descriptor;
- generated Range factories and traits where supported.

The raw library remains responsible for concrete algorithms such as allocation, probing, balancing and storage management. CMeta generates typed structure and metadata, not the underlying container algorithm.

Multiple instantiations may be written as:

```c
Containers(
    (List, UserList, User),
    (Vec, UserVec, User),
    (HashMap, UsersById, int, User)
);
```

## Multi-translation-unit model

Generated wrapper functions are TU-local `static inline`. Generated descriptors may also be TU-local.

Descriptor addresses are not stable program-wide type IDs. Consumers must compare semantic type identity/content rather than require pointer equality.

This rule becomes more important as generic type applications gain structured TypeId metadata.

## Range metadata

CMeta provides an allocation-free borrowed `cmeta_range` protocol with traits such as:

```text
SIZED
ORDERED
SORTED
UNIQUE
CONTIGUOUS
RANDOM_ACCESS
REUSABLE
```

Typed sequence containers can derive a default Range. Associative containers can publish key/value/entry Range factories.

## Interfaces and Callables

CMeta supports interface/protocol declaration independently of generic generation:

```c
interface(Source, ...);
implements(...);
```

`implements(...)` means a concrete type implements an interface/protocol. It is unrelated to the removed container implementation phase.

`cmeta_callable` is the shared Command abstraction for typed behavior. It carries signature, effects, properties and captures and is reused by domains such as CFlow and future CMeta State/Exec modules.

## Hexagonal architecture

CMeta follows Ports & Adapters / Functional Core + Imperative Shell principles.

```text
                         CMeta Extend
                       syntax adapter
                            │
                            ▼
                    ┌────────────────┐
                    │   CMeta Core   │
                    │ semantic kernel│
                    └────────────────┘
                       ▲     ▲     ▲
                       │     │     │
                    State   Exec  CFlow
                             │
                    coroutine/platform ports
                             │
                    minicoro / native OS
```

Core must not depend on Extend, State, Exec, CFlow, minicoro or native OS code.

First-party modules own their own semantic invariants. Extend may provide nicer syntax but remains an adapter.

## Build and bootstrap

Core must remain buildable with a plain C11 compiler.

The optional extended-source path is conceptually:

```text
.cm source
  -> cmc
  -> generated C11
  -> GCC / Clang / MSVC
```

A lexer generator such as re2c may be used to build `cmc`, but it is not a Core or application runtime dependency.

## Layering principle

```text
finite preprocessor/generation kernel
              ↓
      Core semantic objects
              ↓
     first-party modules
              ↓
  ordinary C runtime libraries

optional CMeta Extend
              ↓
      lowers into the same APIs
```

The architectural objective is a small, deterministic, typed semantic kernel with replaceable adapters and optional modern syntax.
