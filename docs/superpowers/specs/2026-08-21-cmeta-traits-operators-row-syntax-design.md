# CMeta Traits / Operators Structured Row Syntax Design

## Status

Implemented and verified. The C11 surface syntax and normalization boundary described here are active on `leanv4`; strict-C11 GCC/Clang witnesses, negative compile probes, unchanged generated C/Lean semantic snapshots, and `lake build --wfail` verify the Phase 1 migration.

## Goal

Unify CMeta declaration syntax around a small tuple-row grammar while preserving the existing executable and formal semantics.

The design addresses two readability problems in the current surface:

- `Traits(name, flags, equal, hash, compare, copy, move, destroy)` is an 8-position declaration whose flags duplicate the function slots.
- CFlow operator rows currently encode 15 positional fields in one flat tuple, even though those fields already belong to distinct semantic groups.

The desired result is not a new macro language. Existing `Enum`, `Struct`, `typed`, `interface`, `implements`, `Schema`, and `Replay` roles remain intact.

## Existing compatibility constraint

Today the public preprocessor kernel defines:

```c
#define Replay(schema, M) schema(M)
#define Operators(M, ...) Schema(M, __VA_ARGS__)
```

and the CFlow operator provider is shaped as:

```c
#define CFlowOperators(M) \
    Operators(M, ...rows...)
```

This design **preserves that provider/consumer contract**. It does not repurpose `Operators` into `Operators(Name, ...)` in Phase 1.

Therefore the approved structured source form is expressed inside the existing provider:

```c
#define CFlowOperators(M) \
    Operators(M, \
        (MAP, map, \
            (call,     1, 0, -1), \
            (fn,       1, INPUT, NONE, NONE, VALUE), \
            (flow,     RETURN, ONE, NONE), \
            (semantic, map), \
            (effect,   CMETA_EFFECT_PURE)))
```

This keeps `Replay(CFlowOperators, Consumer)` unchanged and prevents a surface cleanup from becoming an unrelated schema-provider redesign.

A future named-declaration syntax such as `OperatorSchema(CFlowOperators, ...)` may be considered separately if it proves useful, but it is not part of this migration.

## Non-goals

This slice does not:

- change `cmeta_type_traits` runtime representation;
- change CFlow operator descriptor semantics;
- change operator identifiers, callable ABI, graph semantics, cardinality semantics, effects, or lowering;
- change the `CFlowOperators(M)` / `Replay(CFlowOperators, M)` provider-consumer model;
- add default inference for omitted operator properties;
- merge function bodies into operator declarations;
- expose Lean judgments such as `Supports` or `Candidate` as user-facing C macros;
- introduce `Item(...)`, `Field(...)`, `Equal(...)`, `Hash(...)`, `Arity(...)`, `Effect(...)`, or similar wrapper vocabulary;
- change `typed(...)`, lambda, bind, or callable execution semantics;
- introduce a second operator universe alongside `CFlowOperators`.

## Design principle

CMeta declaration syntax follows one tuple-row grammar:

```text
schema        ::= declaration(row, row, ...)
row           ::= homogeneous-row
                | tagged-row
                | composite-row

tagged-row   ::= (tag, payload...)
composite-row ::= (identity..., tagged-row, tagged-row, ...)
```

The schema already supplies context, so a row should not be wrapped in a second noun when its role is clear.

For homogeneous schemas:

```c
Struct(Point,
    (double, x),
    (double, y)
);
```

is preferred to:

```c
Struct(Point,
    Field(double, x),
    Field(double, y)
);
```

For heterogeneous property sets, the first tuple element is a tag:

```c
Traits(Point,
    (equal, point_equal),
    (hash, point_hash),
    (copy, point_copy)
);
```

For composite objects, identity fields are followed by tagged subrows:

```c
(FILTER, filter,
    (call,     1, 0, -1),
    (fn,       1, INPUT, NONE, NONE, BOOL),
    (flow,     SAME, FILTER, NONE),
    (semantic, filter),
    (effect,   CMETA_EFFECT_PURE))
```

## Traits surface

### Current form

```c
Traits(Point,
    CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COPY,
    point_equal,
    point_hash,
    NULL,
    point_copy,
    NULL,
    NULL
);
```

The same information is encoded twice: flags declare capability presence while positional function slots repeat the presence and hide the semantic role behind position.

### Proposed form

```c
Traits(Point,
    (equal, point_equal),
    (hash, point_hash),
    (copy, point_copy)
);
```

A fully populated declaration is:

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

The public row tags are lowercase because they describe semantic properties rather than generated global C identifiers.

### Trait normalization

The public rows normalize mechanically to the existing `cmeta_type_traits` payload:

```text
(equal, f)   -> CMETA_TRAIT_EQUAL   + .equal = f
(hash, f)    -> CMETA_TRAIT_HASH    + .hash = f
(compare, f) -> CMETA_TRAIT_COMPARE + .compare = f
(copy, f)    -> CMETA_TRAIT_COPY    + .copy_construct = f
(move, f)    -> CMETA_TRAIT_MOVE    + .move_construct = f
(destroy, f) -> CMETA_TRAIT_DESTROY + .destroy = f
```

The flags are derived from declared rows. Users do not repeat the capability set manually.

Existing `TRIVIAL_COPY` and `TRIVIAL_DESTROY` facts remain outside this row inference in Phase 1. Their declaration/inference requires a separate design because they are properties, not function witnesses.

### Duplicate and unknown tags

The implementation must reject duplicate semantic rows:

```c
Traits(Point,
    (equal, point_equal),
    (equal, point_equal_again)
);
```

and unknown tags:

```c
Traits(Point,
    (serialize, point_serialize)
);
```

Silently taking the first or last row would make declaration order observable and would weaken the capability model.

## Operators surface

### Current flat row

The current CFlow schema contains rows equivalent to:

```c
(FILTER, filter,
 1, 0, -1,
 1, INPUT, NONE, NONE, BOOL,
 SAME, FILTER, NONE,
 filter,
 CMETA_EFFECT_PURE)
```

This is a useful normalized replay representation but an increasingly poor source representation.

### Proposed structured row

Phase 1 keeps every existing field but groups fields by meaning:

```c
#define CFlowOperators(M) \
    Operators(M, \
        (FILTER, filter, \
            (call,     1, 0, -1), \
            (fn,       1, INPUT, NONE, NONE, BOOL), \
            (flow,     SAME, FILTER, NONE), \
            (semantic, filter), \
            (effect,   CMETA_EFFECT_PURE)), \
        (MAP, map, \
            (call,     1, 0, -1), \
            (fn,       1, INPUT, NONE, NONE, VALUE), \
            (flow,     RETURN, ONE, NONE), \
            (semantic, map), \
            (effect,   CMETA_EFFECT_PURE)), \
        (FLAT_MAP, flatMap, \
            (call,     1, 0, -1), \
            (fn,       3, INPUT, OUT_PTR, CURSOR, GENERATOR), \
            (flow,     POINTEE1, EXPAND, NONE), \
            (semantic, flat_map), \
            (effect,   CMETA_EFFECT_PURE)))
```

`Replay(CFlowOperators, Consumer)` remains the canonical consumption syntax.

### `call` subrow

```text
(call, methodArgc, fnArg, childArg)
```

This preserves the existing method-invocation shape exactly. Phase 1 introduces no defaults.

### `fn` subrow

```text
(fn, arity, p0, p1, p2, ret)
```

This preserves the existing callable ABI schema exactly, including `NONE` placeholders for unused positions. Shorter arity-specific forms are deferred until normalization equivalence is proven.

### `flow` subrow

```text
(flow, outputRule, cardinalityRule, childRule)
```

This groups output typing, cardinality, and child coordination without changing any values.

### `semantic` subrow

```text
(semantic, class)
```

This preserves the existing semantic class consumed by graph/optimizer/lowering code.

### `effect` subrow

```text
(effect, value)
```

Phase 1 uses the existing internal constant, for example `CMETA_EFFECT_PURE`. Short aliases such as `PURE` are intentionally deferred; grouping is the semantic cleanup, while token abbreviation is cosmetic and can be decided later.

## Normalization boundary

The key implementation rule is:

> Structured source rows normalize to the current flat operator row before existing CFlow consumers interpret the schema.

Conceptually:

```text
CFlowOperators(M)
      |
      v
structured operator row
      |
      v
single CMeta normalizer
      |
      v
current flat operator row
      |
      +--> enum generation
      +--> descriptors
      +--> callable wrappers
      +--> Stream methods
      +--> graph wrappers
      +--> runtime dispatch
      +--> generated C/Lean witnesses
```

Existing consumers should not each learn how to parse the structured source form.

The current flat row becomes an internal normalized representation rather than the preferred human-written source representation.

## Row ordering

Phase 1 requires operator subrows in canonical order:

```text
call -> fn -> flow -> semantic -> effect
```

Tagged rows make their meaning explicit, but arbitrary-order parsing adds preprocessor complexity without current value.

This syntactic ordering requirement is not a semantic ordering rule: all valid source rows normalize to one fixed record.

For Traits, arbitrary row ordering is desirable only if duplicate detection and flag aggregation remain simple under strict C11 preprocessing. If not, Phase 1 may specify canonical trait order. In either case, two accepted orderings must never produce different semantic descriptors.

## Relationship to `typed(...)`

The operator schema declares the legal operator universe; it does not contain concrete callback bodies.

Concrete callbacks remain separate:

```c
typed(map, value, long, square, (int x)) {
    return (long)x * (long)x;
}
```

The relationship remains:

```text
CFlowOperators
    -> operator kind
    -> callable ABI constraints
    -> flow/cardinality semantics
    -> intrinsic effect class

              +

typed(map, ...)
    -> concrete callable witness
```

This preserves the current architecture in which graph/lowering semantics belong to the operator schema while named callables are first-class concrete values.

## Relationship to formal semantics

The source syntax creates no new Lean judgment.

The formal layer remains downstream:

```text
C source declaration
       |
       v
normalized descriptor / operator schema
       |
       v
IR and static judgments
       |
       v
canonical lowering
```

The migration obligation is normalization equivalence, not textual equivalence.

For Operators:

```text
normalize(structuredOperatorRow) = existingFlatOperatorRow
```

For Traits:

```text
normalize(structuredTraitRows) = existing cmeta_type_traits observable payload
```

Formal/conformance tests should compare normalized observable semantics, not source spelling.

## Compatibility strategy

The migration must not create two semantic implementations.

Recommended transition:

1. Keep the current flat operator row as the internal normalized consumer representation.
2. Add one structured-row normalizer.
3. Migrate `CFlowOperators(M)` rows to the structured form while retaining `Replay(CFlowOperators, M)` unchanged.
4. Add structured `Traits(...)` normalization to the existing `cmeta_type_traits` representation.
5. Migrate representative trait declarations/examples.
6. Verify existing C and Lean semantic observations remain unchanged.
7. Mark legacy positional declaration helpers internal/legacy after all in-repo users migrate.

If a compatibility window is needed, legacy input must normalize into the same canonical representation rather than using a second consumer path.

## Error model

Source errors should fail at compile/preprocess time where practical.

Required failures include:

- unknown Trait tag;
- duplicate Trait tag;
- malformed Trait payload arity;
- malformed Operator composite row;
- unknown Operator subrow tag;
- missing required Operator subrow;
- duplicate Operator subrow;
- malformed Operator subrow arity;
- structured Operator row that cannot normalize to the existing flat schema.

Phase 1 intentionally avoids silent defaults because they weaken diagnostics and make equivalence harder to audit.

## Implementation phases

### Phase 1: structured syntax, zero semantic change

Implement exactly the full current information in structured source form.

Representative source:

```c
Traits(Point,
    (equal, point_equal),
    (hash, point_hash),
    (copy, point_copy)
);

#define CFlowOperators(M) \
    Operators(M, \
        (MAP, map, \
            (call, 1, 0, -1), \
            (fn, 1, INPUT, NONE, NONE, VALUE), \
            (flow, RETURN, ONE, NONE), \
            (semantic, map), \
            (effect, CMETA_EFFECT_PURE)))
```

Normalized output must match the existing descriptor/operator observations.

### Phase 2: repository migration

Migrate all `CFlowOperators` rows and relevant trait examples/documentation. Do not modify callback bodies, graph logic, optimizer logic, plan logic, or runtime logic merely to accommodate source syntax.

### Phase 3: optional compression

Only after equivalence is established, evaluate omissions that can be normalized safely, such as:

- default pure intrinsic effect;
- default no-child rule;
- common method invocation shapes;
- unused callable parameter placeholders.

Each compression needs its own RED/GREEN proof that the shorter source normalizes to the same canonical representation.

## Verification strategy

Implementation uses TDD. The first change must be a RED conformance target that consumes the new syntax before production normalization exists.

GREEN requires at least:

- strict C11 compilation with the formal GCC and Clang presets;
- structured `Traits(...)` producing expected flags and function pointers;
- negative compile probes for duplicate/unknown Trait tags;
- structured `CFlowOperators(M)` normalizing to the same flat rows or equivalent generated observations as today;
- `Replay(CFlowOperators, Consumer)` remaining source-compatible for consumers;
- existing callable signature validation continuing to pass;
- generated C/Lean snapshots unchanged unless a snapshot intentionally records source-only metadata;
- existing graph, optimizer, plan, execution, runtime, registry, and language-spec conformance continuing to pass;
- `lake build --wfail` with no new warning, axiom, `sorry`, or `admit`.

The strongest migration signal is unchanged semantic observations with a more structured declaration surface.

## Design invariants

Implementation is acceptable only if all remain true:

1. `Enum` and `Struct` keep plain homogeneous tuple rows.
2. Heterogeneous properties use tagged tuple rows rather than wrapper vocabulary.
3. Composite rows use identity fields plus tagged semantic subrows.
4. Trait capability flags are derived from declared capability rows rather than repeated manually.
5. `CFlowOperators(M)` remains the authoritative operator provider and `Replay(CFlowOperators, M)` remains the consumer interface.
6. Structured operator source normalizes to one canonical internal flat representation.
7. Existing Replay consumers do not each implement structured parsing.
8. `typed(...)` remains the concrete callable-definition mechanism.
9. CFlow operator and lowering semantics do not change.
10. Formal judgments remain downstream of descriptors/IR and are not exposed as C surface commands.
11. Source-order choices cannot become observable semantic differences.

## Open implementation choices

These are deliberately left to the implementation plan because they do not change the approved semantics:

- whether normalization uses `Schema`, `Replay`, direct tag dispatch, or a small combination;
- whether Phase 1 requires canonical Trait row order for simpler duplicate detection;
- exact internal macro names for normalized operator rows;
- whether legacy positional Trait input is retained temporarily behind an explicitly internal compatibility name.

The public `Operators(M, ...)` provider contract and full effect token spelling are **not** open choices in Phase 1; they remain as today.

## Acceptance example

The intended first-phase style is:

```c
Struct(Point,
    (double, x),
    (double, y)
);

Traits(Point,
    (equal, point_equal),
    (hash, point_hash),
    (copy, point_copy)
);

#define CFlowOperators(M) \
    Operators(M, \
        (FILTER, filter, \
            (call,     1, 0, -1), \
            (fn,       1, INPUT, NONE, NONE, BOOL), \
            (flow,     SAME, FILTER, NONE), \
            (semantic, filter), \
            (effect,   CMETA_EFFECT_PURE)), \
        (MAP, map, \
            (call,     1, 0, -1), \
            (fn,       1, INPUT, NONE, NONE, VALUE), \
            (flow,     RETURN, ONE, NONE), \
            (semantic, map), \
            (effect,   CMETA_EFFECT_PURE)))

typed(map, value, long, square, (int x)) {
    return (long)x * (long)x;
}
```

The central design statement is:

> CMeta source syntax exposes semantic structure; one normalization boundary preserves the canonical representation used for execution and proof.
