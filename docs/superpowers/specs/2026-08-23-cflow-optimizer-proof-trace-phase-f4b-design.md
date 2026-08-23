# CFlow Optimizer Proof Trace Phase F-4B Design

## Context

Phase F-3 matches generated AOT Stage IR with Surface, normalized and
structurally fused Graphs. It deliberately rejects optimized Graphs whose
semantic stages were deleted. Phase F-4A proves in Lean that duplicate
idempotent Map elimination preserves the complete stream observation when the
callable supplies PURE, TOTAL, declared IDEMPOTENT and an independent
idempotence law.

The C optimizer currently exposes only aggregate statistics. A count of deleted
Maps cannot identify which source stage was removed, bind the claim to an exact
optimized Graph or explain the result to the AOT matcher.

## Decision

Add an optional owned `cflow_opt_trace` to optimization. The trace contains a
bounded contiguous sequence of `cflow_opt_rewrite_event` records and immutable
bindings to the exact source/destination Graph objects and versions used by the
optimizer. The first stable rule value is
`CFLOW_OPT_RULE_IDEMPOTENT_MAP_ELIMINATION = 1`.

Each event records both the retained and removed source coordinates:

```c
typedef struct cflow_opt_rewrite_event {
    cflow_opt_rule rule;
    cflow_subgraph_id source_subgraph;
    cflow_node_id retained_node;
    size_t retained_callable_index;
    cflow_node_id removed_node;
    size_t removed_callable_index;
} cflow_opt_rewrite_event;
```

The trace representation is opaque and move-only by contract. A zero-state
trace is accepted by `cflow_graph_optimize_with_trace`; success commits one
owned implementation object, failure leaves it zero. `cflow_opt_trace_destroy`
is idempotent. The existing `cflow_graph_optimize` delegates to the same core
with tracing disabled and retains its API and allocation behavior.

`cflow_aot_pipeline_ir_match_optimized_graph` accepts the original normalized
Graph, optimized Graph and trace. It first proves the Stage IR exactly matches
the original Graph. It then validates trace identity/version binding, replays
each event against the source coordinates, rechecks callable identity and the
declared pure/total/idempotent endomap contract, removes only certified logical
stages, and matches the remaining Stage IR against the optimized Graph.

This C checker validates that the implementation instantiated the rule recorded
by the trace. It does not prove that arbitrary C machine code satisfies the
mathematical idempotence law; that remains the callable producer/Lean witness
boundary documented by F-4A.

## Memory and Lifecycle Protocol

- **Data unit:** one fixed-size rewrite event; it contains IDs and indices only.
- **Fact source:** the normalized source Graph and the optimizer output Graph.
- **Ownership:** a successful trace owns one contiguous allocation containing
  its header and event capacity. Callers destroy it exactly once; repeated
  destroy is safe. Graphs own no trace storage.
- **Borrowing:** the trace stores Graph object identity and versions but never
  dereferences those pointers through public accessors. Matching borrows both
  live Graphs for the call and compares identity/version before reading events.
- **Invalidation:** Graph mutation, destruction, struct copying or moving makes
  the binding unusable. Version mismatch or object mismatch fails explicitly.
- **Capacity:** event capacity equals the checked count of logical callables in
  the source Graph. Each elimination consumes one slot; overflow, capacity
  exhaustion and allocation failure abort optimization transactionally.
- **Topology:** synchronous, single-threaded control-plane producer and
  consumer. No cross-thread mutation or synchronization is provided.
- **Order:** events are emitted in optimizer traversal order. Replay requires
  the retained stage to be the nearest preceding non-removed stage.
- **Failure:** no partial trace is published and the destination Graph follows
  the existing all-or-destroyed optimizer failure contract.
- **Shutdown:** destroy the trace and Graphs only while quiescent. Trace destroy
  does not access either Graph.
- **Observability:** count and indexed event access are public; invalid indices
  fail without modifying the output event.

## Compatibility and Performance

All APIs are additive. The public Graph, callable, Plan, Direct evaluator and
existing witness layouts do not change. The new optimized witness is a distinct
type. Existing optimizer callers use the no-trace core and incur no new
allocation or traversal.

Trace construction adds one bounded allocation and O(number of logical
callables) counting only when explicitly requested. AOT certificate replay uses
fixed arrays bounded by `CFLOW_AOT_STAGE_LIMIT == 16`. No trace work occurs in
Direct, Plan or Kernel per-item execution.

## Verification

- An exact two-stage idempotent Direct schema optimizes to one Map and emits one
  event with exact retained/removed coordinates.
- The ordinary F-3 matcher rejects the shortened Graph; the certificate-aware
  matcher accepts it and commits original stage count, applied rewrite count and
  both Graph versions.
- Surface, optimized Kernel and generated Direct evaluation produce identical
  ordered values for negative, zero and positive inputs.
- Replaying a trace against a cloned or mutated Graph fails and clears the
  witness.
- Zero-state/repeated trace destruction is safe; event out-of-range access
  fails transactionally.
- MSVC and Clang CMeta/CFlow matrices pass. Direct benchmark throughput must not
  regress by more than 10%; the expected code path is unchanged.

## Rollback

Remove the additive trace declarations/core option, certificate-aware matcher,
tests and this phase documentation. Existing optimization, F-3 Stage IR and all
execution paths remain independently usable.
