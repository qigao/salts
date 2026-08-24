# CFlow Trivial Value Admission Design

## Context

CMeta already exposes `CMETA_TRAIT_TRIVIAL_COPY` and
`CMETA_TRAIT_TRIVIAL_DESTROY`. CFlow's Direct/AOT surface checks both traits,
but the resumable runtime, byte-array result collector, generic Plan executor,
array source, Range source, readiness source, and channel currently move values
through uninitialized byte storage.

Those paths use `memcpy`, callback writes, or whole-buffer `free` without
calling `copy_construct`, `move_construct`, or `destroy`. Accepting a type that
owns memory or another resource can therefore duplicate an owner and omit its
destructor.

`cmeta_container_data()` is unrelated to physical storage: it returns the
container's semantic `cmeta_data_desc`. The current Range ABI has no checked
contiguous-storage callback, so this change must not infer a raw data pointer
from that API or from `CMETA_RANGE_CONTIGUOUS` alone.

## Decision

Introduce one internal CFlow value-storage admission policy:

```c
required = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
```

The policy is applied at every boundary that can admit values into the current
byte-storage execution model:

- array, Range, channel, and readiness source construction;
- one-shot value, coordination, SubRun, and Relation resumable construction;
- generic runtime open, including user-defined `cflow_source` implementations;
- generic Plan support and compilation.

Graph and Stream construction remain type-generic. A non-trivial graph can
still be described, normalized, inspected, and extended by a future
lifecycle-aware executor. Only execution through the current byte-storage
engines is rejected.

Direct/AOT retains its existing public admission helper and semantics.

## Ownership and failure protocol

- Source constructors allocate no state and leave `out` unchanged when the
  type is unsupported.
- Channel initialization leaves the channel zero when the type is unsupported.
- Failed `cflow_run_open[_subgraph]` does not move the Source; the caller still
  owns and must destroy it.
- Unsupported-type coordination admission does not move any child resumable.
- Failed Plan compilation leaves `plan->impl == NULL` and records a diagnostic.
- Adapter failures preserve their existing transactional zero-output behavior.
- No fallback to byte copying is permitted.

The runtime remains externally serialized at the value callback boundary.
This change adds no cross-thread ownership transfer and no new allocation.

## Compatibility

This intentionally rejects inputs that were previously admitted but could not
be executed safely. `cmeta_type_size` gains only the two storage properties
needed by Timer execution; it deliberately remains without the callable
COPY/MOVE/DESTROY traits required by CMeta Collectors. Existing CBind target
failure semantics therefore remain unchanged.

No public structure layout, function signature, dependency, token format, or
CBind behavior changes.

## Deferred work

- `SIZED` terminal preallocation requires benchmark evidence before changing
  allocation behavior.
- A `CONTIGUOUS` fast path requires a separate versioned physical storage-view
  contract. `cmeta_container_data()` cannot provide it.
- Trait-aware non-trivial execution requires construction/destruction coverage
  for source scratch values, every intermediate frame, result arrays,
  collectors, cancellation, and every failure path. It is not implemented as a
  fallback in this change.
- `SORTED` and `UNIQUE` optimizer facts remain deferred until CFlow has
  operations whose semantics can consume them and an identity for the relevant
  comparator/equality relation.

## Verification

Tests must prove:

- array, Range, channel, and readiness constructors reject an owning type;
- one-shot value, coordination, and SubRun constructors reject an owning type;
- runtime rejects a user-defined non-trivial Source and preserves its ownership;
- generic Plan support and compilation reject a non-trivial Graph;
- an overflow test still reaches the overflow branch by using a trivial
  three-byte descriptor;
- existing CFlow runtime, pipeline, graph, direct, scheduler, and execution
  behavior remains green;
- installed public headers remain C and C++ compatible.
