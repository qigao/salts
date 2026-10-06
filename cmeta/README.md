# CMeta v50

声明生成新增入口：有限 PP 原语见 [`pp.h`](include/cmeta/pp.h)，编译器约束见
[`compiler.h`](include/cmeta/compiler.h)，精确函数 thunk 见
[`invoke_decl.h`](include/cmeta/invoke_decl.h)。使用与边界详见
[`LANGUAGE_REFERENCE.md`](LANGUAGE_REFERENCE.md#有限宏与精确调用声明)。

**Finite generic programming, typed metadata, and semantic code generation for strict C11.**

CMeta is the semantic foundation of Salts. It uses standard C11 techniques such
as finite macro specialization and generic selection where appropriate to make
strongly typed APIs practical without introducing a managed runtime or changing
native C object layout. It provides type identity, metadata, traits, interfaces,
contracts, ranges, typed callables, and finite compile-time computation.

CMeta is intentionally finite and schema-driven. It does not try to reproduce
unrestricted C++ templates or invent a universal replacement language before
concrete use cases require it.

**Tags:** C11 · generic-programming · metadata · reflection · traits · interfaces · contracts · code-generation

The authoritative syntax and layering contract is
[`LANGUAGE_REFERENCE.md`](LANGUAGE_REFERENCE.md). Compiler/tooling reflection
inspection follows [`INSPECTION.md`](INSPECTION.md): immutable descriptors plus
canonical semantic queries, without a second meta object model or runtime RTTI.

[`FASTPATH.md`](FASTPATH.md) specifies bounded C11 static keys and typed static
calls, their publication/provider lifetimes, and explicitly enabled x86_64 /
AArch64 assembly backends. Portable atomics remain the default implementation.

## Public programming model

Application users normally use semantic DSLs:

```c
cmeta_struct(User,
    cmeta_field(int, id)
    cmeta_field(double, score)
);

cmeta_enum(State,
    (READY, "ready"),
    (DONE,  "done")
);

cmeta_traits(User,
    (equal, user_equal),
    (hash, user_hash),
    (copy, user_copy)
);

cmeta_type(Option, MaybeUser, User);
cmeta_type(Pair, UserScore, User, double);
```

Trait rows are the only supported public `cmeta_traits` declaration form. CMeta derives
both capability flags and function slots from those tagged rows. Positional
`Traits(name, flags, ...)` compatibility has been removed.

Named `cmeta_trait(Capability, callback)` rows, compile-time trait/field
requirements, generated tagged variants and distinct unsigned64 flags reuse
this canonical metadata and DataDesc lifecycle. See
[capabilities, variants and flags](CAPABILITIES.md) for contracts and executable
C11/C++17 examples.

`cmeta_type(...)` is the finite-generic type declaration entry point. CMeta owns value kinds
such as `Pair`, `Tuple`, `Option`, and `Result`. Container kinds such as `List`,
`Vec`, and `HashMap` are provided by `container`, not by the CMeta aggregate
header.

`cmeta_struct` and `cmeta_field` generate layout and Reflection metadata from
one declaration. Tuple rows remain framework machinery, not the canonical
application field syntax.

`cmeta_scope` owns an explicit finite resource set and runs a status-returning
body function. Native early returns from that function still reach generated
LIFO cleanup. Scope construction and cleanup use the same canonical DataDesc
construct ops through a generated native-typed static lifecycle accessor.
`cmeta_scope_checked` explicitly admits hand-written/runtime descriptors through
the existing checked lifecycle binding; it uses the same cleanup lowering.
There is no automatic fallback or second lifecycle registry. See
[structured scope and migration](LANGUAGE_REFERENCE.md#structured-scope).

Explicit lifecycle facts let `(Type, value, trivial)` scope rows omit callback,
ops-pointer and live-state machinery. Admitted lifecycle/invokable/field bindings
support repeated use without validating immutable metadata graphs again.
Data, ObjectRef and Plugin adapters share finite lexical cleanup obligations;
their existing resource authorities remain separate. See
[lifetime admission and lowering](LIFECYCLE_LOWERING.md).

`cmeta_scope_nofail` omits per-resource live/ops state for statically declared
INIT_NOFAIL resource sets, including managed CSTL values. Body failures still
perform LIFO cleanup; C++ body exceptions clean up before propagating.

`<cmeta/data_select.h>` provides `cmeta_data_of(pointer)` for builtin Data
descriptors and `cmeta_data_of_in(pointer, schema)` for explicit local schemas.
The pointer is an unevaluated type witness. Both C11 and C++17 select the same
canonical descriptor, reject unknown/volatile pointer types, and evaluate only
the selected descriptor expression once. See [typed selection](LANGUAGE_REFERENCE.md#schema-driven-data-selection).

`<cmeta/bind.h>` generates receiver and ordinary parameter binding with explicit
scalar snapshots or borrowed pointers, exact native thunks, and canonical
Function projections. Captures stay within `CMETA_CAPTURE_INLINE`; projected
signatures use the existing unary/binary registry. See
[parameter binding](RECEIVER_OPERATIONS.md#生成-receivercapturebind976-2729).

Execution/runtime ownership is explicit. Atomics and RCU are owned by
Salts::Concurrency. Pool storage/lease policy is owned by Salts::Core and
thread-affinity/TLS policy by Salts::Platform. CMeta retains only explicit
lifecycle adapters where DataDesc semantics add value:
`cmeta_pool_type(Name, Type)` and `cmeta_local_type(Name, Type)`.
See [ownership, capacity, suspension and memory-order contracts](EXECUTION_PRIMITIVES.md).

## Semantic string and byte storage adapters

`cmeta/data.h` keeps STRING/BYTES meaning separate from native storage layout.
A full `cmeta_data_desc` may point at versioned `cmeta_data_buffer_ops` that
define semantic-zero construction and detection, bounded assignment,
no-fail move into a zero destination, restore-to-zero, and an
optional bounded borrowed read view. The checked facade validates adapter ABI,
storage identity plus exact physical layout, owned/borrowed agreement, the
provider's returned pointer/size pair, and the caller's byte ceiling before
publishing read outputs.

Assignment is single-owner and failure-atomic: the destination must start in
the provider's semantic zero state, and a failed assignment is restored to
that state. The adapter is format-neutral and does not interpret UTF-8 or
source-reader lifetimes. A format binder must enforce those rules before it
passes a byte slice to CMeta.

A successful read does not copy or extend ownership. Its view expires when the
source object is mutated, assigned, restored, destroyed, concurrently changed,
or otherwise invalidated by the provider. The v2 lifecycle requires both
`init_zero` and `move`; v1 providers are rejected. Read remains optional and
an otherwise complete provider without it returns `CMETA_TRAIT_MISSING`.

`CMETA_DEFINE_FIXED_BYTES` supplies this lifecycle for a complete inline byte
type, alongside the exact native-value provider and copy/move/destroy traits.
Its extent must equal the native type's size and must be nonzero. Assignment
requires exactly that extent (`CMETA_TYPE_MISMATCH` for another length) and
obeys the byte budget (`CMETA_CAPACITY_EXCEEDED`). Read borrows the entire
inline extent, including an all-zero value; semantic zero does not mean an
empty span. Copy produces independent storage, move clears the source, and
restore is idempotent. These operations allocate nothing and require exclusive
access to mutated objects.

`<cmeta/fixed_array.h>` provides `CMETA_DEFINE_FIXED_ARRAY` for an inline array
typedef. It exposes `CMETA_DATA_SEQUENCE` with a constant element count and
contiguous borrowed iteration, while preserving the existing C array ABI.
Every slot is live even in semantic zero. Element zero initialization must be
allocation-free and no-fail; copy, move and cleanup dispatch to the element's
canonical provider. Copy failure releases the accepted prefix and any partial
element; move leaves every source element in semantic zero. A collector accepts
exactly the declared count, rejects a short finish with `CMETA_TYPE_MISMATCH`,
and rejects excess input or an insufficient item budget with
`CMETA_CAPACITY_EXCEEDED`. There is no extra native count field, metadata
allocation or ownership registry. Element payload allocation remains governed
by the element provider's byte budget.

For example, a consumer linked with `Salts::CMeta` can use:

```c
#include <cmeta/fixed_array.h>

typedef int readings[3];
CMETA_DEFINE_FIXED_ARRAY(readings_value, readings, int, 3u, &cmeta_data_int,
                         "example.Readings3", "Readings3");

int main(void) {
    readings source = {1, 2, 3};
    readings destination;
    const cmeta_data_desc *data = &readings_value_cmeta_data;
    if (cmeta_data_value_init_zero(data, destination) != CMETA_OK) return 1;
    if (cmeta_data_value_move(data, destination, source) != CMETA_OK) return 1;
    cmeta_data_value_destroy(data, destination);
    cmeta_data_value_destroy(data, source);
    return 0;
}
```

`cmeta_data_value_is_zero` queries the canonical lifecycle zero without
changing ownership. A fixed array's zero means zero elements in every live
slot, rather than an empty borrowed range. Record queries cover reflected
fields; a consumer still owns any unreflected presence/default overlay bytes.
Unsupported zero providers return an error and never infer zero from native
pointer or descriptor identity. These additions use optional size-versioned
collection callbacks; older provider prefixes retain their existing behavior.

Salts Core provides header-local `tstr` and `vstr` adapter metadata in
`cmeta_cmeta_data.h`. As with other header-generated CMeta metadata, descriptor
addresses may differ across translation units; use semantic type comparison.

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
instantiate several generic types; write one `cmeta_type(...)` declaration per
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

Libraries register a finite generic kind. Concrete type declarations use:

```c
cmeta_type(kind, generated_name, type_arguments...);
```

`cmeta_type(...)` accepts only registered generic kinds and routes directly to
their finite `CMETA_TYPED_` provider. It does not fall through to callable or
operator DSLs.

Callable/function DSLs use their own explicit entry points such as CFlow's
`cmeta_function(...)`; generic type routing never falls through to them.

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

The built-in five type rows and finite `8/2/1` unary, binary, and generator
relations have one source of truth:
`formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/BuiltinSignatures.lean`.
Lean validates that manifest and generates
`include/cmeta/generated/builtin_signature_manifest.h`; do not edit the
generated header by hand. Regenerate or verify it from
`formal/cmeta_cflow_calculus`:

```text
lake exe cmeta-signature-gen --write ../../cmeta/include/cmeta/generated/builtin_signature_manifest.h
lake exe cmeta-signature-gen --check ../../cmeta/include/cmeta/generated/builtin_signature_manifest.h
```

The checked-in header means ordinary CMake builds and installed CMeta headers
do not require Lean. Application-defined `CMETA_USER_TYPE_LIST` and
`CMETA_USER_*_RELATION_LIST` macros remain manual shared configuration.

Signature lowering exposes `CMETA_VALUE_SIGNATURES(U, B)` for unary and binary
value callables, while `CMETA_GENERATOR_SIGNATURES(G)` names the generator
protocol. Runtime invoke/generate dispatch uses these protocol-specific groups;
the complete ABI surface continues to use `CMETA_ALL_SIGNATURES(U, B, G)`.
This removes protocol-unreachable generated branches without changing signature
IDs or rejection results.

## Multi-TU model

Generated wrapper functions are TU-local `static inline`, and generated
metadata may also be translation-unit local. Consumers must compare descriptors
semantically rather than requiring pointer equality across translation units.

These views and all reachable callbacks are borrowed from their provider. A
dynamic module must remain loaded until every descriptor/interface/callable
consumer finishes, including copies retained by CFlow graphs/plans. Before
exchanging native descriptors, negotiate `CMETA_REFLECTION_ABI_VERSION`; the
runtime query `cmeta_reflection_abi_version()` checks linked-library agreement.
Reflection epochs do not version callable configurations or application vtables.
See [native module lifetime and ABI rules](LANGUAGE_REFERENCE.md#reflection-across-native-modules)
for the bootstrap and unload contract and its dynamic-library regression test.

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

Concrete libraries such as `container` may expose Range views through their own
typed adapters and descriptors.

Read-only reflection uses the narrower CMeta Data borrowed cursors. Collection
cursors return one source-owned element pointer; map cursors return source-owned
key/value pointers without allocating, copying, or exposing a native iterator.
When a provider supplies a generation callback, any mutation after `begin`
terminates traversal with `CMETA_GEN_MUTATED`. Pointers remain valid only while
the source remains alive and unmodified.

## Transactional collectors

`cmeta_collector` is the bounded, single-threaded collection protocol for an
adapter that constructs a caller-owned, zero-initialized output. A concrete
container descriptor may expose an optional value-oriented factory:

```c
cmeta_collector (*collector)(void *zero_output, size_t limit);
```

A provider may instead expose the optional `collector_init` tail callback.
`cmeta_data_collection_collector` initializes it directly in the final caller
slot, allowing fixed arrays to borrow the collector's own count without
allocating a separate context. Such a collector must not be copied or moved
before termination. Its output and slot remain alive until finish/abort;
discarding it before begin retains no resources. The older factory remains
available for providers using the established value-oriented construction.

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

An Interface is a typed protocol/capability view, not the canonical runtime
identity/lifetime container for an arbitrary native object. Runtime-selected
identity and BORROWED/SHARED/OWNED authority live in `cmeta_object_ref`.
Projecting one object into an Interface therefore creates a borrowed capability
relation over the same provider; it must not copy the native object or infer
ownership from `self`, the vtable layout, or descriptor equality.

Interfaces containing `CMETA_INTERFACE_METHOD_OWNS_SELF` are ownership-bearing
capabilities. A borrowed ObjectRef-to-Interface projection must reject them
unless ownership is explicitly transferred; otherwise Interface destruction
could invalidate an object while its ObjectRef still claims to be live.
`cmeta_interface_desc_has_owning_method()` is the canonical semantic query for
that admission decision. Static compile-time-known Interface calls remain direct
typed vtable dispatch and do not route through ObjectRef.

Interface method schemas have two reflection levels. Historical `R0..R4`,
`V0..V4`, and `D0` rows define the exact dispatch ABI only; their method
reflection does not invent FunctionDesc semantics. Frameworks that need shared
function semantics use `F0..F4`, `FV0..FV4`, or `FD0` rows and explicitly
provide contract, return descriptor/carrier, and five-field parameter rows.
Those methods expose canonical `cmeta_function_desc` and
`cmeta_function_abi_desc` through the interface metadata, so TinyMock, service
binding, and execution-admission consumers do not maintain an interface-only
signature registry.

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

Typed tracepoint、一次性 fault point 和 disabled fast path 的使用与生命周期见 [TRACEPOINTS.md](TRACEPOINTS.md)。


Portable discovery and ABI contracts are documented in
[STATIC_MANIFESTS.md](STATIC_MANIFESTS.md), [FINGERPRINTS.md](FINGERPRINTS.md)
and [PLUGIN_MANIFESTS.md](PLUGIN_MANIFESTS.md). Runtime ownership and installed
Reflection-only qualification follow [BOUNDARIES.md](BOUNDARIES.md).
