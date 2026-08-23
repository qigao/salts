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

## Finite compile-time computation

CMeta can name finite type and integer-constant relations without introducing
C++ template syntax:

```c
TypeFunction(CommonArithmetic,
    (int, int, int),
    (int, double, double),
    (double, int, double),
    (double, double, double));

ValueFunction(TypeRank,
    (int, 1),
    (double, 2));

Predicate(Hashable,
    (int, 1),
    (opaque, 0));

typedef TypeEval(CommonArithmetic, int, double) result_type;
enum { double_rank = ValueEval(TypeRank, double) };
Require(Hashable, int);
```

`TypeFunction` and `ValueFunction` infer unary through ternary arity from the
first row. `TypeEval` and `ValueEval` infer it from their input count, so callers
never select a numbered entry point. `Predicate`, `Satisfies`, and `Require`
provide boolean queries and compile-time constraints. A missing or conflicting
row is a compile error; there is no default mapping. Numbered public spellings
are not exposed.

Function names and input keys must each be one stable preprocessor identifier.
Each declaration accepts 1 through 16 rows; repeat a declaration with the same
function name to add bounded fragments. The first row determines arity and all
rows in the declaration must match it. Values must be integer constant
expressions. Use a stable typedef name for types or declarators that contain
commas or other preprocessing syntax.

Existing schemas can also be folded as constant expressions:

```c
#define FeatureChecks(M) Schema(M, (1), (1), (0))

enum {
    feature_count = SchemaCount(FeatureChecks),
    every_feature = SchemaAll(FeatureChecks),
    some_feature = SchemaAny(FeatureChecks)
};
```

`SchemaCount` accepts any non-empty row shape. `SchemaAll` and `SchemaAny`
require exactly one integer constant expression per row.

### Finite DFA inference

The same row list can also project a bounded C relation for admission-time
inference:

```c
#define CommonRows \
    (TYPE_SMALL, TYPE_SMALL, TYPE_SMALL), \
    (TYPE_SMALL, TYPE_WIDE, TYPE_WIDE)

ValueFunction(CommonType, CommonRows);
InferenceRules(common_type_rules, CommonRows);
```

`cmeta_infer_dfa_build` converts the explicit relation to a deterministic
prefix trie using caller-owned state and transition arrays. A successful DFA
query is bounded by the declared arity (1 through 3). Missing, duplicate,
ambiguous, invalid, and capacity failures are distinct and never select a
default result.

The DFA is a control-plane mechanism: build it during validation, admission,
or plan compilation, then store only the inferred type/action in the execution
plan. Do not query it for every data item. CMeta performs no hidden allocation;
the relation borrows its static rows and the DFA borrows caller workspace.

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
