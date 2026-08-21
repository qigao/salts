# CMeta Traits / Operators Structured Row Syntax Design

## Status

Design approved for specification. This document defines the intended C11 surface syntax and normalization boundary only. It does not authorize implementation until this spec is reviewed.

## Goal

Unify CMeta declaration syntax around a small tuple-row grammar while preserving the existing executable and formal semantics.

The design specifically addresses two readability problems in the current surface:

- `Traits(name, flags, equal, hash, compare, copy, move, destroy)` is an 8-position declaration whose flags duplicate the function rows.
- CFlow operator rows currently encode 15 positional fields in one flat tuple, even though those fields already belong to distinct semantic groups.

The desired result is not a new macro language. The result should make existing schema structure explicit while keeping `Enum`, `Struct`, `typed`, `interface`, and `implements` conceptually unchanged.

## Non-goals

This slice does not:

- change `cmeta_type_traits` runtime representation;
- change CFlow operator descriptor semantics;
- change operator identifiers, callable ABI, graph semantics, cardinality semantics, effects, or lowering;
- add default inference for omitted operator properties;
- merge function bodies into `Operators(...)`;
- expose Lean judgments such as `Supports` or `Candidate` as user-facing C macros;
- introduce `Item(...)`, `Field(...)`, `Equal(...)`, `Hash(...)`, `Arity(...)`, `Effect(...)`, or similar wrapper vocabulary;
- change `typed(...)`, lambda, bind, or callable execution semantics;
- introduce a second operator universe alongside `CFlowOperators`.

## Design principle

CMeta declaration syntax follows one grammar:

```text
schema      ::= Declaration(Name, row, row, ...)
row         ::= homogeneous-row
              | tagged-row
              | composite-row

tagged-row ::= (tag, payload...)

composite-row ::= (identity..., tagged-row, tagged-row, ...)
```

The important rule is that the schema name already supplies context. A row should not be wrapped in a second noun when its role is already clear.

Therefore:

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

For objects with multiple semantic groups, use tagged subrows:

```c
Operators(CFlowOperators,
    (MAP, map,
        (call,     1, 0, -1),
        (fn,       1, INPUT, NONE, NONE, VALUE),
        (flow,     RETURN, ONE, NONE),
        (semantic, map),
        (effect,   PURE)
    )
);
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

The declaration encodes the same information twice:

- the flags say which capabilities exist;
- the positional function slots say which capabilities exist.

The position of a function is also meaningful but invisible at the call site.

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

The public row tags are intentionally lowercase because they describe semantic properties rather than C identifiers generated into the global namespace.

### Trait normalization

The public declaration normalizes to the existing runtime representation:

```text
(equal, f)   -> flag CMETA_TRAIT_EQUAL   + .equal = f
(hash, f)    -> flag CMETA_TRAIT_HASH    + .hash = f
(compare, f) -> flag CMETA_TRAIT_COMPARE + .compare = f
(copy, f)    -> flag CMETA_TRAIT_COPY    + .copy_construct = f
(move, f)    -> flag CMETA_TRAIT_MOVE    + .move_construct = f
(destroy, f) -> flag CMETA_TRAIT_DESTROY + .destroy = f
```

Existing trivial flags remain descriptor-level facts unless and until a separate design proves a better declaration syntax for them. This design does not infer `TRIVIAL_COPY` or `TRIVIAL_DESTROY` from the presence or absence of functions.

The normalized result remains a `cmeta_type_traits` object with the existing fields and flags. No consumer should need to understand the new source syntax.

### Duplicate and unknown tags

The syntax must reject:

```c
Traits(Point,
    (equal, point_equal),
    (equal, point_equal_again)
);
```

and must reject unknown tags such as:

```c
Traits(Point,
    (serialize, point_serialize)
);
```

Silently taking the first or last row would make the declaration order semantically observable and would undermine the existing capability model.

## Operators surface

### Current normalized row

The current CFlow operator schema contains rows equivalent to:

```c
(FILTER, filter,
 1, 0, -1,
 1, INPUT, NONE, NONE, BOOL,
 SAME, FILTER, NONE,
 filter,
 CMETA_EFFECT_PURE)
```

This representation is compact for replay machinery but poor as a source-level declaration because unrelated categories are flattened together.

### Proposed structured row

The first version of the new surface keeps every existing field but groups them explicitly:

```c
Operators(CFlowOperators,

    (FILTER, filter,
        (call,     1, 0, -1),
        (fn,       1, INPUT, NONE, NONE, BOOL),
        (flow,     SAME, FILTER, NONE),
        (semantic, filter),
        (effect,   PURE)
    ),

    (MAP, map,
        (call,     1, 0, -1),
        (fn,       1, INPUT, NONE, NONE, VALUE),
        (flow,     RETURN, ONE, NONE),
        (semantic, map),
        (effect,   PURE)
    ),

    (FLAT_MAP, flatMap,
        (call,     1, 0, -1),
        (fn,       3, INPUT, OUT_PTR, CURSOR, GENERATOR),
        (flow,     POINTEE1, EXPAND, NONE),
        (semantic, flat_map),
        (effect,   PURE)
    )
);
```

The groups map directly to the existing fields.

### `call` subrow

```text
(call, methodArgc, fnArg, childArg)
```

This preserves the existing method-invocation shape.

No default is introduced in the first implementation.

### `fn` subrow

```text
(fn, arity, p0, p1, p2, ret)
```

This preserves the existing callable ABI schema.

The first implementation keeps the existing maximum positional representation even when a lower arity leaves `NONE` slots. A later simplification may define shorter arity-specific row forms, but that must be a separate semantic-preservation step.

### `flow` subrow

```text
(flow, outputRule, cardinalityRule, childRule)
```

This groups output typing, cardinality, and child coordination without changing their values.

### `semantic` subrow

```text
(semantic, class)
```

The semantic classification remains exactly the existing value used by graph/optimizer/lowering consumers.

### `effect` subrow

```text
(effect, value)
```

The source surface may use short values such as `PURE` only if the normalization layer maps them mechanically to the existing internal constant such as `CMETA_EFFECT_PURE`.

The first implementation may instead retain the full internal token in this subrow if short-token mapping adds unnecessary scope. The important design invariant is grouping, not spelling.

## Normalization boundary

The key implementation rule is:

> The new syntax must normalize to the current flat operator row before existing CFlow consumers replay it.

Conceptually:

```text
structured source row
        |
        v
CMeta row normalizer
        |
        v
existing flat operator row
        |
        +--> enum generation
        +--> descriptors
        +--> callable wrappers
        +--> Stream methods
        +--> graph wrappers
        +--> runtime dispatch
        +--> generated C/Lean witnesses
```

This prevents every consumer from learning the new surface independently.

The normalized representation remains the compatibility boundary for framework-internal replay.

The first implementation should therefore prefer one normalization macro family plus unchanged existing consumers over rewriting every consumer for structured rows.

## Row ordering

Within one operator declaration, tagged subrows should have a canonical source order in the first implementation:

```text
call -> fn -> flow -> semantic -> effect
```

Although the rows are tagged, arbitrary-order parsing in the C preprocessor would add complexity without current user value.

This is intentionally different from semantic dependence on order. The source parser may require canonical order while the normalized semantic object remains the same fixed record.

Traits rows may be allowed in any order only if duplicate detection and flag aggregation remain simple and deterministic. If arbitrary ordering complicates the implementation materially, the first version may document a canonical trait order as well. Semantic identity must never depend on which valid source order was used.

## Relationship to `typed(...)`

`Operators(...)` declares the legal operator universe. It does not define concrete callbacks.

Concrete callbacks remain separate:

```c
typed(map, value, long, square, (int x)) {
    return (long)x * (long)x;
}
```

The relationship is:

```text
Operators(...)
    -> operator kind
    -> legal callable ABI
    -> flow/cardinality semantics
    -> intrinsic effect class

             +

typed(map, ...)
    -> concrete callable witness
```

This preserves the existing design in which graph and lowering reason about operator semantics while named callables remain first-class concrete values.

## Relationship to formal semantics

The new source syntax must not create new Lean judgments.

The existing formal layer already separates:

```text
syntax / descriptors
        |
        v
static judgments
        |
        v
canonical lowering
```

This design only changes the C declaration surface used to construct the same descriptors/operator schema.

The formal obligation for the migration is normalization equivalence:

```text
normalize(structuredRow) = existingFlatRow
```

For Traits, the obligation is descriptor equivalence:

```text
normalize(Traits rows) = existing cmeta_type_traits payload
```

No proof should state that textual source forms are equal. The proof/conformance target is the normalized observable descriptor/schema.

## Compatibility strategy

The migration should not keep two independent semantic implementations.

Recommended transition:

1. Keep existing normalized flat consumer macros as the internal representation.
2. Add structured source normalization.
3. Migrate `CFlowOperators` to structured rows.
4. Migrate representative trait declarations.
5. Verify generated C/Lean observations remain unchanged.
6. Remove or clearly mark the old public positional declaration form as internal/legacy once all in-repo users have migrated.

If a short compatibility window is needed, legacy syntax should normalize into the same internal representation rather than taking a separate consumer path.

## Error model

Source mistakes should fail at compile/preprocess time where practical.

Required failures include:

- unknown Trait tag;
- duplicate Trait tag;
- malformed Trait payload arity;
- unknown Operator subrow tag;
- missing required Operator subrow;
- duplicate Operator subrow;
- malformed Operator subrow arity;
- structured operator row that cannot normalize to the existing flat schema.

The implementation should avoid silent defaults in the first phase because they weaken diagnostics and make equivalence harder to audit.

## Implementation phases

### Phase 1: syntax normalization with zero semantic change

Implement only enough machinery to express the full current schema structurally.

Target source:

```c
Traits(Point,
    (equal, point_equal),
    (hash, point_hash),
    (copy, point_copy)
);

Operators(CFlowOperators,
    (MAP, map,
        (call, 1, 0, -1),
        (fn, 1, INPUT, NONE, NONE, VALUE),
        (flow, RETURN, ONE, NONE),
        (semantic, map),
        (effect, CMETA_EFFECT_PURE)
    )
);
```

The normalized output must equal the current flat schema/descriptor observations.

### Phase 2: migrate repository declarations

Migrate `CFlowOperators`, trait examples, and relevant documentation/examples.

Do not alter callable bodies or graph/runtime code as part of this phase.

### Phase 3: optional syntax compression

Only after equivalence is established, evaluate removing values that are provable defaults, for example:

- default pure intrinsic effect;
- default no child rule;
- common method-argument shapes;
- unused callable parameter placeholders.

Any such simplification requires its own tests proving the shorter surface normalizes to the same canonical schema.

## Verification strategy

This design relies on the repository's existing real-C plus Lean verification stack.

Implementation must use TDD and should introduce a RED conformance target that expects the new syntax before production macros exist.

GREEN must include at least:

- strict C11 compilation under both GCC and Clang formal presets;
- `Traits(...)` structured rows generating the expected flags and function pointers;
- negative compile tests for duplicate/unknown trait tags;
- structured `CFlowOperators` producing the same normalized operator descriptors as the current schema;
- existing callable signature checks continuing to pass;
- existing generated C/Lean snapshots unchanged unless the snapshot intentionally records surface-only metadata;
- existing graph, optimizer, plan, execution, and runtime conformance continuing to pass;
- `lake build --wfail` with no new warnings, axioms, `sorry`, or `admit`.

The strongest migration signal is that the existing generated semantic observations do not change while source declarations become structured.

## Design invariants

The implementation is acceptable only if all of the following remain true:

1. `Enum` and `Struct` continue to use plain homogeneous tuple rows.
2. Heterogeneous properties use tagged tuple rows, not wrapper vocabulary.
3. Composite declarations use identity fields plus tagged subrows.
4. `Traits` capability flags are derived from declared capability rows rather than duplicated manually.
5. `Operators` source rows normalize to one canonical internal row representation.
6. Existing Replay consumers do not each implement their own structured parser.
7. `typed(...)` remains the concrete callable-definition mechanism.
8. CFlow operator lowering semantics do not change.
9. Formal judgments remain downstream of descriptors/IR and are not exposed as source DSL commands.
10. No source-order choice becomes an observable semantic difference.

## Open implementation choices

These choices are intentionally left to the implementation plan because they do not change the approved surface semantics:

- whether the normalizer is implemented with `Schema`, `Replay`, direct preprocessor dispatch, or a small combination;
- whether Phase 1 requires canonical Trait row ordering for simpler duplicate detection;
- whether `effect` uses `PURE` or `CMETA_EFFECT_PURE` in the first migration;
- the exact internal macro names used for normalized operator rows;
- whether legacy positional forms are retained temporarily behind an explicitly internal compatibility macro.

## Acceptance example

The following should be representative of the intended final first-phase CMeta/CFlow declaration style:

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

Operators(CFlowOperators,
    (FILTER, filter,
        (call,     1, 0, -1),
        (fn,       1, INPUT, NONE, NONE, BOOL),
        (flow,     SAME, FILTER, NONE),
        (semantic, filter),
        (effect,   CMETA_EFFECT_PURE)
    ),
    (MAP, map,
        (call,     1, 0, -1),
        (fn,       1, INPUT, NONE, NONE, VALUE),
        (flow,     RETURN, ONE, NONE),
        (semantic, map),
        (effect,   CMETA_EFFECT_PURE)
    )
);

typed(map, value, long, square, (int x)) {
    return (long)x * (long)x;
}
```

The central design statement is:

> CMeta source syntax should expose semantic structure, while normalization preserves one canonical internal representation for execution and proof.
