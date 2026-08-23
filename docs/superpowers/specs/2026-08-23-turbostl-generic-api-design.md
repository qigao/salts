# TurboSTL Generic API Design

**Date:** 2026-08-23

## Context

TurboSTL currently exposes two competing application-level forms. Its documentation describes finite CMeta instantiation such as `typed(List, IntList, int)`, while `turbostl/typed.h` actually exposes instance declarations such as `List(int, values)`. CMeta's finite-kind router and TurboSTL's header-local facade generators still exist, but TurboSTL no longer registers its kinds. The Stream facade consequently collects into an erased instance rather than a declared output type.

## Decision

TurboSTL has one application-level generic model:

```c
typed(List, IntList, int);
typed(Map, IntLongMap, int, long);
```

The declaration emits a concrete C type, header-local typed forwarding functions, container metadata, Range views, and a collector. Operations use the declared type token:

```c
IntList values = {0};
list_init(IntList, &values, 100u);
list_add(IntList, &values, 7);
list_destroy(IntList, &values);
```

Stream terminals also require that type token:

```c
result = to_list(&pipeline, IntList, &values, 100u);
result = collect(&pipeline, IntList, &values, 100u);
```

The erased `vec_t`, `list_t`, `map_t`, and related implementations remain the compiled storage/algorithm layer and ABI. They are available through focused component headers. `turbostl/typed.h` does not expose a second `Vec(T, variable)`/`Map(K, V, variable)` declaration language.

## Architecture and state ownership

- CMeta owns finite generic routing through `typed(kind, ...)` and `CMETA_TYPED_CALL`.
- TurboSTL owns the thirteen kind registrations, the kind schema, and typed facade generation.
- Each generated wrapper owns one raw TurboSTL handle. The wrapper's CMeta header is the source of its public type and Range/collector metadata.
- The compiled raw handle owns allocated storage. Successful generated `destroy` releases that storage and invalidates the generated wrapper descriptor.
- CFlow owns stream evaluation. TurboSTL only supplies a collector constructed from the explicitly named output type.
- Public generated collector factories accept the concrete wrapper pointer;
  descriptor-based erased adapters are confined to the CMeta metadata boundary.

## Error, capacity, and lifetime semantics

- Generated operations preserve existing `stl_status` results.
- Initialization and collection limits remain mandatory and explicit.
- A Stream source is borrowed and must remain alive and unmodified until the terminal finishes.
- Collection is transactional: overflow, type mismatch, or callback failure aborts and restores a zero output wrapper.
- A mismatched output wrapper is diagnosed by the generated collector function
  signature before the erased CFlow boundary.
- No fallback from typed operations to erased instance inference is provided.

## Compatibility and migration

This intentionally changes the application-level API. Existing `Vec(int, values)` declarations migrate to `typed(Vec, IntVec, int); IntVec values = {0};`. Existing erased code can keep using focused raw headers and raw handles. Aggregate and typed-facing examples/tests migrate to the finite Generic API.

Generated `Type_method` symbols remain available as the concrete typed ABI, while semantic kind operations are the documented application facade. No `.cmeta` frontend, angle-bracket syntax, parser, or code-generation step is introduced.

## Alternatives considered

- Keep both declaration forms: rejected because it leaves two equal public models and makes ownership/type identity depend on which syntax a caller selected.
- Infer the type solely from an erased output instance: rejected because Stream signatures no longer state their output type and compile-time type checking is lost.
- Add C++-style `Map<int, long>` syntax: impossible in C11 without a new frontend and explicitly outside this change.

## Verification and rollback

Verification covers compile-time instantiation of all thirteen kinds, representative typed operations, Range/collector behavior, Stream collection, installed-header consumption, and the adjacent CMeta/CFlow suites. The change can be rolled back by reverting the kind-registration/schema and test migrations; raw algorithms and storage formats are unchanged.
