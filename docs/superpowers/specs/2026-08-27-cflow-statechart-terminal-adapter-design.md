# CFlow Statechart Terminal Adapter Design

## Context

Issue #125 identifies that the native Statechart runtime is not yet a
first-class participant in the CFlow runtime. The original Phase 1 plan
promised a Source/Resumable terminal projection, but the public Statechart
model has no observation declaration or value-output callback. Inventing a
state-snapshot stream here would therefore add an unrelated public semantic
decision.

This slice adds the missing terminal projection only. Value observations and
the Statechart Actor facade remain separate follow-up work under #125.

## Decision

Add two explicit APIs:

```c
bool cflow_statechart_instance_as_terminal_resumable(
    cflow_statechart_instance *instance,
    cflow_resumable *out);

bool cflow_statechart_instance_as_terminal_source(
    cflow_statechart_instance *instance,
    cflow_source *out);
```

Both adapters borrow one live Statechart instance and expose only:

- `WAIT` while the instance is active;
- `DONE` after clean completion, close, or cancellation settles;
- `ERROR` with the instance-owned first error after failure.

They never return `VALUE` or `VALUE_AND_DONE`. The Statechart state descriptor
is the empty sequence's element-type witness, which permits type admission by
the existing runtime without defining a state-snapshot emission policy.

Only one Resumable or Source adapter may be attached to an instance at once.
Destroying the adapter cancels and detaches it but does not free the instance.
Destroying an instance while an adapter remains attached returns
`CFLOW_STATECHART_RUNTIME_WOULD_BLOCK` and preserves the handle.

## Data-path protocol

| Property | Contract |
|---|---|
| Data unit | No value payload; terminal readiness only. |
| Fact source | `cflow_statechart_instance_impl` remains the sole mutable fact source. |
| Ownership | The instance owns all state and errors. The adapter borrows the instance. A moved Source transfers only the adapter handle, not instance ownership. |
| Lifetime | Adapter operations are valid until adapter destroy. The instance and immutable Statechart must outlive the adapter. |
| Topology | External event admission remains MPSC; one SerialExecutor owns semantic mutation; one downstream adapter/waiter is admitted. |
| Ordering | Terminal readiness becomes observable only after the existing commit-versus-cancel settlement rule has produced `done`, except errors which remain immediately observable through the existing first-error boundary. |
| Capacity | No new queue, payload slot, or growth path is introduced. |
| Backpressure | Not applicable to values in this slice; no value is emitted or dropped. |
| Failure | The adapter projects the stable instance error. Invalid or occupied outputs and a second adapter fail without mutation. |
| Shutdown | Source/Resumable cancel requests Statechart cancellation. Destroy cancels, clears registered wakers, and detaches. Instance destroy requires prior detach. |
| Observation | Existing Statechart stats and error APIs remain authoritative. |

## Compatibility

The change is additive. Existing Machine, hierarchy, Statechart, Graph, Run,
Executor, Scheduler, and Actor behavior is unchanged. The explicit `terminal`
name prevents later value-observation work from silently changing this
adapter's empty-stream contract.

No XML, SCXML data model, communication, durable workflow, state snapshot
stream, implicit worker, fallback execution mode, or unbounded allocation is
introduced.

## Verification

- API admission rejects invalid, occupied, and second adapter destinations.
- Resumable follows `WAIT -> DONE` and wakes once after close settles.
- Source reports `OPEN -> DONE` and `OPEN -> ERROR` with stable error text.
- Destroy refuses a still-attached adapter and succeeds after detach.
- A Source moved into `cflow_run` wakes and completes an identity Graph.
- Existing Statechart runtime, CFlow runtime, Machine, hierarchy, Actor, and
  aggregate C/C++ header tests continue to pass.
