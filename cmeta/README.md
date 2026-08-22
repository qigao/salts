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

Traits(User,
    (equal, user_equal),
    (hash, user_hash),
    (copy, user_copy)
);

typed(Option, MaybeUser, User);
typed(List, UserList, User);
typed(Vec, UserVec, User);
typed(HashMap, UserMap, int, User);
```

Trait rows declare capabilities once: CMeta derives both the trait flags and the corresponding function slots from the tagged rows. `TRIVIAL_COPY` and `TRIVIAL_DESTROY` remain explicit descriptor properties rather than inferred callable traits.

For typed containers, declaration is complete instantiation. There is no public container `implement(...)` phase and no separate batch-container declaration DSL.

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

Application code does not need a separate schema or batch declaration to instantiate several generic types; write one `typed(...)` declaration per concrete type. This keeps the public generic surface to one entry point while leaving `Schema/Replay` as framework-generation machinery.

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

Multiple instantiations are simply multiple declarations:

```c
typed(List, UserList, User);
typed(Vec, UserVec, User);
typed(HashMap, UsersById, int, User);
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

## Transactional collectors

`cmeta_collector` is the bounded, single-threaded collection protocol for an
adapter that constructs a caller-owned, zero-initialized output. A container
descriptor may expose an optional value-oriented factory:

```c
cmeta_collector (*collector)(void *zero_output, size_t limit);
```

`begin` receives the input descriptor and hard item limit. `accept` borrows one
value only until the callback returns; the adapter must copy, retain, or move it
within that call. `finish` commits the output, while `abort` releases temporary
values and restores the output's documented zero state.

```c
cmeta_collector collector = descriptor->collector(&output, limit);
if (cmeta_collector_begin(&collector) != CMETA_OK) return false;
for (size_t i = 0; i < count; ++i) {
    if (cmeta_collector_accept(&collector, &cmeta_type_int, &values[i]) != CMETA_OK)
        return false; /* the facade has already aborted exactly once */
}
return cmeta_collector_finish(&collector) == CMETA_OK;
```

The stable state vocabulary is `ZERO`, `BEGUN`, `ACCEPTING`, `COMMITTED`, and
`ABORTED`. `finish` is valid only from `BEGUN` or `ACCEPTING`; an empty collection
commits directly from `BEGUN`. `accept` checks `count >= limit` before dispatch,
so zero capacity is valid for an empty result and `SIZE_MAX` cannot wrap. Values
must have a semantically equal `cmeta_type_desc`; pointer identity is not used.

Any validation or callback failure after `begin`, including an unknown callback
status (mapped to `CMETA_CALLBACK_ERROR`), records the first error and invokes
adapter `abort` exactly once. Repeated abort is harmless; begin, accept, or finish
after a terminal state returns `CMETA_INVALID_ARGUMENT` without calling adapters.
The façade performs no allocation, I/O, logging, retry, or synchronization;
callers must externally serialize lifecycle mutation.

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
