# CMeta v50

CMeta is a pragmatic modern-C dialect/toolkit built on strict C11: a finite,
schema-driven compile-time metadata/code-generation layer plus ordinary C
runtime protocols. It does not attempt to reproduce unrestricted C++ templates
or design a universal language before concrete use cases require it.

The authoritative syntax and layering contract is
[`LANGUAGE_REFERENCE.md`](LANGUAGE_REFERENCE.md).

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
typed(Pair, UserScore, User, double);
```

Trait rows are the only supported public Traits declaration form. CMeta derives
both capability flags and function slots from those tagged rows. Positional
`Traits(name, flags, ...)` compatibility has been removed.

`typed(...)` is the single finite-generic entry point. CMeta owns value kinds
such as `Pair`, `Tuple`, `Option`, and `Result`. Container kinds such as `List`,
`Vec`, and `HashMap` are provided by `turbostl`, not by the CMeta aggregate
header.

## Unified Schema / Replay kernel

Framework authors can define row schemas with:

```c
#define MyRows(M) \
    Schema(M, \
        (ROW_A, 1), \
        (ROW_B, 2))

Replay(MyRows, SOME_MAPPER)
```

`Schema` owns parenthesized-row unpacking. `Replay` applies a named schema to a
mapper. `Enum`, `Struct`, `Traits`, and CFlow's structured operator metadata
reuse this kernel internally.

Application code does not need a separate schema or batch declaration to
instantiate several generic types; write one `typed(...)` declaration per
concrete type.

## Finite generic routing

Libraries register a finite generic kind. The common entry point:

```c
typed(kind, ...)
```

routes a registered kind to its matching `CMETA_TYPED_` registration macro.
Unregistered kinds can fall through to a framework-provided typed fallback;
CFlow uses that path for lowercase operator callables such as `typed(map, ...)`.

There is no `Containers(...)` batch DSL and no container `implement(...)`
generation phase.

## Type identity and type universes

CMeta distinguishes two finite type sets:

```text
known types      -> descriptors / reflection / generic type identity
callable types   -> generated callable signature families
```

This avoids expanding every reflected generic application into the full
callable Cartesian product.

`cmeta_type_desc` may carry a semantic `cmeta_type_identity`. Built-in atoms,
pointers, const forms, and generic applications compare through stable semantic
identity rather than descriptor address. Header-generated descriptor addresses
are therefore not process-global type IDs.

Legacy projects that define only `CMETA_TYPE_LIST` keep the historical behavior:
that list acts as both known and callable type universes. New code should use
`CMETA_KNOWN_TYPE_LIST` and `CMETA_CALLABLE_TYPE_LIST` when the distinction
matters.

## Multi-TU model

Generated wrapper functions are TU-local `static inline`, and generated
metadata may also be translation-unit local. Consumers must compare descriptors
semantically rather than requiring pointer equality across translation units.

## Range metadata

CMeta provides an allocation-free borrowed `cmeta_range` protocol with traits
including:

```text
SIZED
ORDERED
SORTED
UNIQUE
CONTIGUOUS
RANDOM_ACCESS
REUSABLE
```

Concrete libraries such as `turbostl` may expose Range views through their own
typed adapters and descriptors.

## Transactional collectors

`cmeta_collector` is the bounded, single-threaded collection protocol for an
adapter that constructs a caller-owned, zero-initialized output. A concrete
container descriptor may expose an optional value-oriented factory:

```c
cmeta_collector (*collector)(void *zero_output, size_t limit);
```

`begin` receives the input descriptor and hard item limit. `accept` borrows one
value only until the callback returns; the adapter must copy, retain, or move it
within that call. `finish` commits the output, while `abort` releases temporary
values and restores the output's documented zero state.

The stable state vocabulary is `ZERO`, `BEGUN`, `ACCEPTING`, `COMMITTED`, and
`ABORTED`. The façade performs no allocation, I/O, logging, retry, or
synchronization policy.

## Interfaces

CMeta supports small protocol/vtable interfaces:

```c
interface(Source, SOURCE_METHODS);
implements(Source, file_source, capabilities,
    .next = file_source_next,
    .close = file_source_close);
```

This is a `{ self, vtable }` protocol mechanism, not a class hierarchy.

## Layering principle

```text
ordinary C
       ↓ when repeated declaration/metadata boilerplate appears
Application DSL
       ↓ for reusable compile-time row generation
Framework DSL
       ↓ for execution/storage/interoperation
Runtime Protocol
```

Prefer composing existing CMeta mechanisms and ordinary C over adding new
language vocabulary prematurely.
