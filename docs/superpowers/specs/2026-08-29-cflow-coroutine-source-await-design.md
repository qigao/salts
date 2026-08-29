# CFlow Coroutine Source Await Design

## Decision

Add an optional `CFlowMinicoro` adapter that lets a running stackful coroutine
await an existing `cflow_source`. The Source, including an IO Source backed by
an Actor, remains the authoritative state machine. The coroutine adapter only
translates Source `WAIT` into coroutine suspension and returns the next
non-`WAIT` Source step to the coroutine entry function.

This change does not replace the Actor, create another completion queue, or
change the native backend thread model. A later externally-driven backend may
reduce backend threads; coroutine syntax alone does not.

## Public Interface

```c
cflow_step cflow_minicoro_await_source(cflow_minicoro *coroutine,
                                        cflow_source *source,
                                        void *out_value);
```

The function may be called only from the active coroutine entry callback.
It repeatedly calls `cflow_source_resume(source, active_context, out_value)`.
For `WAIT`, it suspends with the exact Source waitable and retries after the
owner resumes the coroutine. It returns only `VALUE`, `VALUE_AND_DONE`,
`DONE`, or `ERROR`.

Parameters and result:

- `coroutine` is the currently running frame supplied to its entry callback.
- `source` is a valid borrowed Source that remains stable across suspension.
- `out_value` is empty, correctly sized and aligned storage for the Source
  output type.
- `VALUE` and `VALUE_AND_DONE` construct one live value in `out_value`;
  `DONE` leaves it empty; `ERROR` leaves it empty and provides borrowed error
  text. Invalid arguments, managed output types, invalid step kinds, invalid
  waitables, and suspension failures return `ERROR` without fallback.

Example entry callback:

```c
typedef struct read_entry_state {
    cflow_source *source;
} read_entry_state;

static void read_one(cflow_minicoro *coroutine, void *user) {
    read_entry_state *state = (read_entry_state *)user;
    int value = 0;
    cflow_step step = cflow_minicoro_await_source(
        coroutine, state->source, &value);

    if (step.kind == CFLOW_STEP_VALUE ||
        step.kind == CFLOW_STEP_VALUE_AND_DONE) {
        (void)cflow_minicoro_return_value(coroutine, &value);
    } else if (step.kind == CFLOW_STEP_ERROR) {
        (void)cflow_minicoro_fail(
            coroutine, step.error != NULL ? step.error : "Source failed");
    }
}
```

The owner constructs a `cflow_resumable` with
`cflow_resumable_from_minicoro()`, resumes it through the normal CFlow
Resumable contract, and destroys the borrowed Source only after the coroutine
is cancelled or destroyed.

## Data and Ownership Protocol

| Concern | Contract |
|---|---|
| Data unit | One value constructed by the Source in caller-supplied empty storage. |
| Fact source | `cflow_source` and its underlying Actor/driver state. The coroutine stores no mirrored completion state. |
| Source ownership | Borrowed. The caller creates and eventually destroys the Source. The Source must remain valid from the call through every suspension and through coroutine cancel/destroy while the await is active. |
| Value ownership | On `VALUE` or `VALUE_AND_DONE`, the caller owns the constructed value. Initial admission is limited to `TRIVIAL_COPY | TRIVIAL_DESTROY` Source output types because minicoro frame destruction does not unwind C object lifetimes. |
| Waitable ownership | Borrowed from the Source. It remains valid until resume or cancellation according to the Source contract. |
| Thread topology | Single coroutine owner. `resume`, `cancel`, and `destroy` do not overlap. A backend thread may invoke a waker, but a waker never resumes the frame directly. |
| Capacity/backpressure | No new buffer or queue. Source/Actor admission and capacity errors remain authoritative. |
| Ordering | Exactly the Source's order. The adapter performs no reordering or batching. |
| Errors | Invalid arguments, invalid Source type, invalid `WAIT`, or suspension failure return `CFLOW_STEP_ERROR`. Source errors are returned without fallback. |
| Cancellation | While suspended on a Source, coroutine cancel calls `cflow_source_cancel()` exactly once and does not separately cancel the same waitable. The caller destroys the borrowed Source after coroutine quiescence. |
| Shutdown | Stop new work, cancel/destroy the outer Resumable, drain/close the Source owner as its contract requires, destroy the Source, then destroy backend resources. |
| Observability | The benchmark reports this as a distinct coroutine-adapter layer and records that it adds zero worker threads. |

## State Mapping

```text
coroutine RUNNING
    -> Source VALUE / VALUE_AND_DONE / DONE / ERROR
    -> return that step to the entry callback

coroutine RUNNING
    -> Source WAIT(waitable)
    -> coroutine SUSPENDED(waitable, borrowed Source)
    -> owner observes wake and resumes coroutine
    -> Source resume

coroutine SUSPENDED(waitable, borrowed Source)
    -> coroutine cancel/destroy
    -> Source cancel
    -> coroutine TERMINAL
```

The IO request lifecycle remains owned by Actor/IO Source:

```text
FREE -> SUBMITTED -> SUSPENDED -> COMPLETED -> READY
     -> RESUMED -> ACKNOWLEDGED -> FREE
```

The adapter does not add transitions to that machine.

## Compatibility and Validation

The API is additive and lives only in the optional `TurboUtils::CFlowMinicoro`
target. Core CFlow, Source, Actor, backend ABI, and default
`CFLOW_ENABLE_MINICORO=OFF` behavior are unchanged.

Validation covers immediate value, `WAIT`/wake/context reuse, cancellation of
the authoritative Source exactly once, invalid `WAIT`, invalid step kind,
terminal error, and trivial-lifecycle admission. A same-work mock benchmark compares direct Source
consumption with coroutine Source await and reports absolute nanoseconds per
value; the result is evidence about adapter overhead, not proof of fewer native
I/O threads.
