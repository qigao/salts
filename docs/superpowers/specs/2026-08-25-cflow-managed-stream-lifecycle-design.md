# CFlow Managed Stream Lifecycle Design

## Background

CFlow's interpreted runtime already represents the execution topology and every
node's input/output type in one normalized Graph. Sources and collectors can
construct and destroy managed CMeta values, but intermediate operator,
continuation, coordination, SubRun, and relation storage still retains raw
bytes. Admission therefore rejects every managed graph containing an operator.

The goal is to make the interpreted `Stream<T>` pipeline lifecycle-aware without
changing existing trivial-value behavior or pretending that byte-oriented
compiled plans and `to_array()` can own non-trivial values.

## Decision

All interpreted retained values use one internal `cflow_value_slot` state:

```text
EMPTY --copy/move/construct--> LIVE --move/destroy--> EMPTY
  |                              |
  +----------- destroy ----------+
```

A slot owns its allocation and, only while `live` is true, exactly one value.
The CMeta type descriptor is the sole source of copy, move, and destroy
semantics. Trivial values stay on the existing `memcpy` fast path.

The resumable contract is strengthened without changing its ABI: `resume()` is
given empty storage and constructs a live value only for `VALUE` or
`VALUE_AND_DONE`. `WAIT`, `DONE`, and `ERROR` leave the storage empty. A failed
typed callable must likewise leave its result storage empty.

## Ownership by execution stage

| Stage | Input | Output / retained state |
|---|---|---|
| Range Source | borrows container element | copy-constructs one live source value |
| Run Source slot | owns | moves into a path slot |
| filter | borrows path slot during call | preserves or destroys the same slot |
| map / transform | borrows input during call | constructs a new slot, then destroys input |
| reduce | consumes the path slot | reducer slot owns accumulator; replacement is transactional |
| flatMap / relation continuation | moves or copies root input into continuation | each resumed value is constructed into an output slot |
| SubRun | owns copied input and one pending output | moves pending output to its caller |
| Coord | owns the latest value of every child | replaces a child only after destroying its previous live value |
| Relation | borrows coordinated child values | constructs a result or owns one last-result slot |
| Sink | borrows during callback | Run destroys the path slot after callback |

Cancellation, error, normal completion, deferred close, and allocation failure
all converge on slot destruction. A successful claim must reach exactly one of
move-to-next-owner or destroy.

## Range integration

Generated CMeta container ranges inspect their element descriptor. Trivial
elements keep byte copy behavior. Managed elements call `copy_construct` and
advertise `CMETA_RANGE_CONSTRUCTS_VALUES`; construction failure returns the
range error immediately. Associative entry ranges use the generated entry type
descriptor so key and value ownership is handled as one aggregate value.

## Compatibility boundary

- Public C structs and function signatures do not change.
- Normalized Graph validation and typed callable compatibility remain intact.
- Interpreted Run, Stream collection, SubRun, coordination, and relation paths
  accept types with complete COPY/MOVE/DESTROY traits.
- Direct/compiled byte-storage plans and `to_array()` remain trivial-only and
  fail admission for managed values.
- Existing trivial pipelines retain `memcpy` behavior and allocation shape
  except where a slot replaces equivalent raw bookkeeping.

## Errors and state consistency

Allocation or construction failure stops the Run with a stable error and leaves
the destination slot empty. The old owner is not released until replacement
construction succeeds. Errors are reported at the Run/sink boundary; internal
layers propagate failure without logging or fallback.

Graph types are immutable facts. Runtime slots are the only mutable ownership
state. Range/container storage and collector results remain separate owners
created through CMeta copy construction.

## Verification

Tests must cover:

1. generated TurboSTL ranges construct managed values and expose the capability;
2. managed filter/map/reduce pipelines balance construction and destruction;
3. cancellation/error paths destroy live source, accumulator, continuation,
   coordination, and relation values exactly once;
4. trivial Stream tests and compiled-plan rejection remain unchanged;
5. Windows preset tests pass locally; Linux verification may run only through
   the configured `root@eu` remote environment.
