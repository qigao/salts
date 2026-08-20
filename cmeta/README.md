# CMeta v50

CMeta is a finite, schema-driven compile-time metadata/code-generation layer for strict C11. It does not attempt to reproduce unrestricted C++ templates.

## Public programming model

Application users normally use semantic DSLs:

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

## Unified Schema / Replay kernel

Framework authors can define row schemas with:

```c
#define MyRows(M) \
    Schema(M, \
        (ROW_A, 1), \
        (ROW_B, 2))

Replay(MyRows, SOME_MAPPER)
```

`Schema` owns parenthesized-row unpacking. `Replay` applies a named schema to a mapper. `Enum`, `Struct`, and CFlow's operator metadata reuse this kernel internally.

Application `Containers(...)` is intentionally *not* a named-schema alias in v50; it directly instantiates every row. This keeps framework replay machinery out of normal application code.

## Finite generic routing

Libraries register a finite uppercase generic kind, for example `List`, `Vec`, or `HashMap`. The common entry point:

```c
typed(kind, ...)
```

routes a registered kind to `CMETA_TYPED_<Kind>`. Unregistered kinds fall through to a framework-provided typed fallback; CFlow uses that path for lowercase operator callables such as `typed(map, ...)`.

No `CMETA_IMPLEMENT_<Kind>` route exists for containers in v50.

## Single-stage typed containers

A library-provided container kind expands a declaration such as:

```c
typed(List, UserList, User);
```

into a complete typed facade containing:

- the concrete wrapper type;
- header-local `static inline` forwarding functions;
- element/key/value type metadata;
- one static container descriptor;
- generated Range factories and traits where supported.

The raw library remains responsible for the concrete algorithm. CMeta generates structural forwarding code, not list allocation logic, hash probing, or B-tree balancing.

Multiple instantiations can be written without an application macro definition:

```c
Containers(
    (List, UserList, User),
    (Vec, UserVec, User),
    (HashMap, UsersById, int, User)
);
```

## Multi-TU model

Container declarations belong in normal include-guarded headers and may be included by many translation units.

Generated wrapper functions are TU-local `static inline`. Generated descriptors are TU-local static metadata. A descriptor pointer stored inside an initialized object may point at the descriptor emitted by the TU that initialized the object; consumers must treat descriptors semantically, not require cross-TU pointer equality.

CMeta's type comparison therefore uses descriptor/type content rather than assuming one process-global descriptor address for every header-instantiated type.

## Range metadata

CMeta provides an allocation-free borrowed `cmeta_range` protocol with traits including:

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

## Interfaces are separate

CMeta still supports:

```c
interface(Source, ...);
implements(...);
```

`implements(...)` means "this concrete type implements an interface/protocol". It is unrelated to the removed container `implement(...)` generation stage.

## Layering principle

```text
preprocessor kernel
       ↓
Schema / Replay
       ↓
semantic DSLs
       ↓
types / metadata / thin typed facades
       ↓
ordinary C runtime libraries
```

Macros are the compile-time mechanism; semantic DSL declarations are the user contract.
