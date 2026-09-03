# CFlow Statechart Runtime Projection and Actor Integration Design

**Issue:** https://github.com/qigao/salts/issues/125

## Decision

Add two additive integration surfaces without changing Statechart semantics:

1. A borrowed terminal projection consisting of `poll_terminal()` and a
   single-arm `cflow_waitable`. It reports only OPEN, DONE, or ERROR and never
   invents a stream value.
2. A Statechart backend for the existing opaque `cflow_actor` handle, selected
   by `cflow_actor_init_statechart()`. Existing Machine Actor APIs and behavior
   remain unchanged.

Do not add an XML parser or a Statechart `cflow_source` in this phase.

## Why there is no Source

The native Statechart executable contract transforms the one instance-owned
extended state and may raise internal Events. It does not construct a typed
downstream observation. A `cflow_source` must advertise one exact
`output_type` and construct one live value for every VALUE result. Active
configuration snapshots are variable-length query results and are not an
implicit stream contract.

The rejected alternatives are:

- emitting extended-state copies after every macrostep, which would introduce
  an undocumented observation frequency and copy policy;
- emitting configuration headers without the complete configuration, which
  would create a lossy second fact source;
- using `void` or a dummy unit type while never producing values, which would
  admit graphs that can never receive a value.

A future Source requires an explicit typed output declaration in the
Statechart IR, action/runtime support that transactionally publishes those
values, and a bounded output queue. Until then, unsupported composition fails
at API selection time rather than falling back to a fabricated stream.

## Public API

`statechart_runtime.h` adds:

```c
typedef enum cflow_statechart_terminal_status {
    CFLOW_STATECHART_TERMINAL_OPEN = 0,
    CFLOW_STATECHART_TERMINAL_DONE,
    CFLOW_STATECHART_TERMINAL_ERROR,
    CFLOW_STATECHART_TERMINAL_INVALID_ARGUMENT
} cflow_statechart_terminal_status;

cflow_statechart_terminal_status
cflow_statechart_instance_poll_terminal(
    const cflow_statechart_instance *instance,
    const char **out_error);

cflow_waitable cflow_statechart_instance_terminal_waitable(
    cflow_statechart_instance *instance);
```

The waitable is borrowed from the instance. It permits one armed waiter. Arm
fails when another waiter is present, invokes inline when terminal is already
published, and cancellation removes the current waiter. The waiter and every
concurrent instance API must be quiescent before destroy.

`actor.h` adds:

```c
typedef struct cflow_statechart_actor_callbacks {
    cflow_error_fn on_error;
    cflow_done_fn on_done;
    void *user;
} cflow_statechart_actor_callbacks;

typedef struct cflow_statechart_actor_config {
    cflow_statechart_instance_config statechart;
    cflow_statechart_actor_callbacks callbacks;
} cflow_statechart_actor_config;

typedef struct cflow_statechart_actor_init_result {
    cflow_actor_status status;
    cflow_statechart_runtime_status statechart_status;
} cflow_statechart_actor_init_result;

typedef struct cflow_statechart_actor_stats {
    cflow_actor_state state;
    cflow_statechart_instance_stats statechart;
    uint64_t rejected_not_started;
    uint64_t rejected_stopping;
    uint64_t rejected_stopped;
    uint64_t rejected_failed;
    uint64_t rejected_stale;
} cflow_statechart_actor_stats;

cflow_statechart_actor_init_result cflow_actor_init_statechart(
    cflow_actor *actor,
    const cflow_statechart_actor_config *config);

bool cflow_actor_get_statechart_stats(
    const cflow_actor *actor,
    cflow_statechart_actor_stats *out);
```

`CFLOW_ACTOR_STATECHART_REJECTED` is appended to
`cflow_actor_status`; existing numeric values do not move.

## Data-path protocol

| Concern | Contract |
| --- | --- |
| Data unit | One copied `cflow_event_view` payload admitted to the existing Statechart external Mailbox. |
| Fact source | The owned `cflow_statechart_instance` remains the only mutable configuration, extended-state, queue, error, and terminal fact source. |
| Ownership | Actor owns the Statechart instance and terminal waitable borrow. Statechart definition, executor, clock, descriptors, bindings, callback users, and timer resources retain their existing borrowed lifetimes. |
| Lifetime | Producer refs retain only the Actor control block. Owner destruction first marks them stale, cancels the terminal waitable, then closes and destroys the Statechart instance before releasing the root ref. |
| Topology | MPMC callers use independently retained Actor refs; the Actor gate serializes admission classification; the Statechart Mailbox accepts copied inputs; one SerialExecutor owns semantic mutation. |
| Order | Accepted external Events preserve Statechart Mailbox FIFO and existing macrostep ordering. The Actor adds no queue or reorder buffer. |
| Capacity | `external_event_capacity`, `internal_event_capacity`, `completion_capacity`, `timer_capacity`, `microstep_limit`, and `max_storage_bytes` remain the only bounds. |
| Backpressure | Mailbox FULL maps exactly to `CFLOW_ACTOR_SEND_FULL`; there is no retry, wait, overwrite, drop, resize, or fallback allocation. |
| Failure | Type mismatch, FULL, lifecycle rejection, executor rejection, runtime error, and stale ref remain distinguishable through send status, init result, runtime stats, Actor state, and first owned error. |
| Shutdown | Stop admission under the Actor gate, publish STOPPING, close the Statechart, let the winning microstep settle, receive one terminal wake, then destroy only after executor quiescence. |
| Observability | `cflow_actor_get_statechart_stats()` combines one Actor-gate snapshot with the existing Statechart accounting snapshot. |

## Lifecycle

- `init_statechart` creates the instance in Actor START. Initial eventless work
  has already stabilized because Statechart initialization is synchronous.
- `start` publishes RUNNING and arms the terminal waitable. An already-final
  initial configuration completes inline and leaves the Actor STOPPED.
- A clean root-final completion or requested close publishes STOPPED and calls
  `on_done` once.
- A Statechart error publishes FAILED, preserves an Actor-owned first-error
  copy, and calls `on_error` once.
- `request_stop` from START closes the instance and publishes STOPPED without
  invoking callbacks, matching existing pre-start Actor behavior.
- `request_stop` from RUNNING publishes STOPPING before close, so reentrant
  terminal notification observes the correct state.
- Owner destruction is serialized, marks producer refs stale before touching
  runtime storage, unarms the waiter, closes, waits/destroys, then releases the
  root reference. Destruction from callbacks remains forbidden.

## Compatibility, migration, and rollback

- Existing Machine Actor construction, scheduling, callbacks, stats, and send
  statuses are unchanged.
- The opaque `cflow_actor` and `cflow_actor_ref` layouts do not change.
- New functions and structs are additive installed-header API. Appending one
  enum member preserves existing numeric values but consumers must rebuild to
  use the new API.
- No graph, Source, SCXML, XML, data-model, or transport dependency is added.
- Rollback removes the additive terminal projection and Statechart Actor
  backend without redirecting existing Machine execution.

## Verification

- Terminal projection: invalid/open/done/error polling, one waiter, duplicate
  arm rejection, cancellation, already-terminal inline wake, and exactly-once
  wake after close/final/error.
- Actor: init rejection, START/RUNNING/STOPPING/STOPPED/FAILED transitions,
  exact type/FULL/lifecycle/stale send mapping, clean final completion,
  initially-final inline settlement, runtime error propagation, retained refs,
  stop paths, and destruction.
- Adjacent regressions: Statechart runtime, Machine runtime, existing Actor,
  Runtime/Source, C and C++ aggregate headers, and installed-package consumers.
