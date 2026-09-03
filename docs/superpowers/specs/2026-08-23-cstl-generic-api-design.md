# Container Generic API Design

**Date:** 2026-08-23

## Context

After PR #53, Container exposes declaration forms such as
`List(int, values)` and expression forms such as `ListOf(int)`, while CMeta's
language reference describes finite instantiation such as
`typed(List, IntList, int)`. CMeta's finite-kind router still exists, but
Container does not register its kinds. The Stream facade consequently collects
into an erased instance rather than a declared output type.

## Decision

Container's concrete-type Generic model is:

```c
typed(List, IntList, int);
typed(Map, IntLongMap, int, long);
```

The declaration emits a concrete C type, header-local typed forwarding
functions, container metadata, Range views, and a collector. Operations use
the generated `Type_method` ABI:

```c
IntList values = {0};
IntList_init(&values, 100u);
IntList_push_back(&values, 7);
IntList_destroy(&values);
```

Typed Stream terminals also require that type token:

```c
result = to_list_typed(&pipeline, IntList, &values, 100u);
result = collect_typed(&pipeline, IntList, &values, 100u);
```

PR #53's `Vec(T, variable)`/`Map(K, V, variable)` declarations and
`VecOf(T)`/`MapOf(K, V)` expressions remain supported as self-describing
erased-handle initializers. They allocate no storage and generate no concrete
C type, so they are not alternative CMeta Generic instantiations. Raw List/Map
operations remain ordinary functions, and the original three-argument Stream
terminals remain available for these handles without arity dispatch.

## Architecture and state ownership

- CMeta owns finite generic routing through `typed(kind, ...)`.
- Container owns the thirteen kind registrations, the kind schema, and typed facade generation.
- Each generated wrapper owns one raw Container handle. The wrapper's CMeta header is the source of its public type and Range/collector metadata.
- The compiled raw handle owns allocated storage. Successful generated `destroy` releases that storage and invalidates the generated wrapper descriptor.
- CFlow owns stream evaluation. Container supplies either a collector constructed
  from the explicitly named generated type or one obtained from an erased
  handle's descriptor.
- Public generated collector factories accept the concrete wrapper pointer;
  descriptor-based erased adapters are confined to the CMeta metadata boundary.

## Error, capacity, and lifetime semantics

- Generated operations preserve existing `stl_status` results.
- Initialization and collection limits remain mandatory and explicit.
- A Stream source is borrowed and must remain alive and unmodified until the terminal finishes.
- Collection is transactional: overflow, type mismatch, or callback failure
  aborts. Generated wrappers are reset to zero; erased handles release storage
  while retaining their descriptor and type binding.
- A mismatched generated output wrapper is diagnosed by the collector function
  signature before the erased CFlow boundary.
- Erased-handle terminals validate the bound container descriptor at runtime.

## Compatibility and migration

This is additive to PR #53: existing `Vec(int, values)` declarations and
`VecOf(int)` expressions continue to compile and retain their erased-handle
semantics. Code that needs a concrete generated type, compile-time output
checking, typed Range entries, or a typed collector can opt into
`typed(Vec, IntVec, int); IntVec values = {0};`.

Generated `Type_method` symbols are the concrete typed ABI. Distinct
`collect_typed`/`to_list_typed` terminals accept an explicit output type without
shadowing raw function names. No alternate frontend, parser, or code-generation
step is introduced.

## Alternatives considered

- Remove PR #53's declaration and expression initializers: rejected because the
  merged feature is compatible when treated as raw-handle initialization rather
  than concrete Generic instantiation.
- Offer only erased output inference: rejected because generated callers would
  lose an explicit result type and compile-time pointer checking. The erased
  terminal remains as the compatibility form rather than the sole form.
- Add syntax that requires a non-C11 frontend: explicitly outside this change.

## Verification and rollback

Verification covers compile-time instantiation of all thirteen kinds, PR #53
declaration/expression compatibility, unshadowed raw List/Map calls,
Range/collector behavior, both Stream terminal forms, installed-header
consumption, and the adjacent CMeta/CFlow suites. Raw algorithms and storage
formats are unchanged.
