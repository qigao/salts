# CFlow Bounded Actor Lifecycle Design

## Context

Issue #68 introduces an Actor boundary over the existing typed `Event` Mailbox,
serialized `Machine` runtime, and `cflow_run` scheduler integration. The Actor is
not a second runtime: it owns one Machine instance and uses one identity Graph
and Run to drive that instance. Existing Mailbox FIFO, Machine transition, and
Run scheduling semantics remain the facts of record.

The feature deliberately excludes supervision, restart, parent/child
hierarchies, remoting, persistence, and mailbox resizing. Those capabilities
remain unavailable rather than being represented by inert or partial APIs.

The Actor/Graph ownership boundary and concurrent Machine cancel/commit
semantics are refined by
`2026-08-24-cflow-actor-runtime-linearization-design.md`. Graph owns typed
output topology, while Actor/Machine/runtime owners retain mutable state and
move-only resources.

## Public API

Add `cflow/include/cflow/actor.h` and include it from `cflow/cflow.h`.

```c
typedef enum cflow_actor_state {
    CFLOW_ACTOR_STATE_START = 0,
    CFLOW_ACTOR_STATE_RUNNING,
    CFLOW_ACTOR_STATE_STOPPING,
    CFLOW_ACTOR_STATE_STOPPED,
    CFLOW_ACTOR_STATE_FAILED
} cflow_actor_state;

typedef enum cflow_actor_status {
    CFLOW_ACTOR_OK = 0,
    CFLOW_ACTOR_INVALID_ARGUMENT,
    CFLOW_ACTOR_INVALID_SCHEDULER,
    CFLOW_ACTOR_MACHINE_REJECTED,
    CFLOW_ACTOR_ALLOCATION_FAILED,
    CFLOW_ACTOR_ALREADY_STARTED,
    CFLOW_ACTOR_STOPPING,
    CFLOW_ACTOR_STOPPED,
    CFLOW_ACTOR_FAILED
} cflow_actor_status;

typedef enum cflow_actor_send_status {
    CFLOW_ACTOR_SEND_ACCEPTED = 0,
    CFLOW_ACTOR_SEND_INVALID_ARGUMENT,
    CFLOW_ACTOR_SEND_TYPE_MISMATCH,
    CFLOW_ACTOR_SEND_FULL,
    CFLOW_ACTOR_SEND_NOT_STARTED,
    CFLOW_ACTOR_SEND_STOPPING,
    CFLOW_ACTOR_SEND_STOPPED,
    CFLOW_ACTOR_SEND_FAILED,
    CFLOW_ACTOR_SEND_STALE
} cflow_actor_send_status;

typedef struct cflow_actor_config {
    cflow_machine_instance_config machine;
    cflow_scheduler *scheduler;
    cflow_sink_callbacks callbacks;
} cflow_actor_config;

typedef struct cflow_actor_init_result {
    cflow_actor_status status;
    cflow_machine_runtime_status machine_status;
} cflow_actor_init_result;

typedef struct cflow_actor_stats {
    cflow_actor_state state;
    cflow_machine_instance_stats machine;
    uint64_t rejected_not_started;
    uint64_t rejected_stopping;
    uint64_t rejected_stopped;
    uint64_t rejected_failed;
    uint64_t rejected_stale;
} cflow_actor_stats;

typedef struct cflow_actor { void *impl; } cflow_actor;
typedef struct cflow_actor_ref { void *impl; } cflow_actor_ref;

cflow_actor_init_result cflow_actor_init(
    cflow_actor *actor, const cflow_actor_config *config);
cflow_actor_status cflow_actor_start(cflow_actor *actor);
cflow_actor_status cflow_actor_request_stop(cflow_actor *actor);
cflow_actor_state cflow_actor_wait(cflow_actor *actor);
cflow_actor_state cflow_actor_current_state(const cflow_actor *actor);
bool cflow_actor_get_stats(const cflow_actor *actor, cflow_actor_stats *out);
const char *cflow_actor_error(const cflow_actor *actor);
bool cflow_actor_ref_acquire(const cflow_actor *actor, cflow_actor_ref *out);
bool cflow_actor_ref_retain(const cflow_actor_ref *ref, cflow_actor_ref *out);
void cflow_actor_ref_release(cflow_actor_ref *ref);
cflow_actor_send_status cflow_actor_ref_try_send(
    const cflow_actor_ref *ref, const cflow_event_view *event);
void cflow_actor_destroy(cflow_actor *actor);
```

`cflow_actor_init_result.machine_status` is `CFLOW_MACHINE_RUNTIME_OK` unless
`status == CFLOW_ACTOR_MACHINE_REJECTED`, in which case it preserves the exact
Machine initialization rejection. Initialization validates that the borrowed
scheduler is valid and advertises `CMETA_SCHED_CAP_CONCURRENT`; manual
schedulers are rejected because a blocking Actor wait must not become a hidden
event-loop pump.

`cflow_actor_wait` blocks until `STOPPED` or `FAILED`. It is a control-plane
operation and must not be called from a Machine action/guard, Actor sink
callback, scheduler worker callback, or concurrently with Actor destruction.

## Ownership and Lifetime

The Actor control block owns:

- one `cflow_machine_instance`, including its fixed-capacity Mailbox and copied
  initial state/binding rows;
- one normalized identity Graph whose source/output type is the Machine output
  type;
- one `cflow_run` after a successful start;
- its lifecycle mutex/condition variable, first-error copy, and counters;
- one root reference retained by the `cflow_actor` owner handle.

It borrows the immutable Machine declaration, SerialExecutor, concurrent
Scheduler, payload type descriptors, guard/action callback functions and user
data, and sink callbacks/user data. Those borrowed objects must remain valid
through `cflow_actor_destroy`; the executor and scheduler must also remain
operational until destruction returns.

Each `cflow_actor_ref` retains the Actor control block independently. Producers
may send concurrently through distinct retained refs. Destroying the owner marks
the control block stale, synchronously closes owned runtime resources, clears
the owner handle, and releases only the root reference. The small control block
is freed after the last producer ref is released. Thus an old ref never
dereferences destroyed Machine/Run storage: it returns
`CFLOW_ACTOR_SEND_STALE`.

Event payloads follow the Machine Mailbox contract. A send borrows the view only
for the call; on acceptance the Actor-owned Mailbox owns a bounded trivial byte
copy. Sink values are borrowed only for the callback duration.

## State Machine and Exact Admission

The only lifecycle transitions are:

```text
START --start succeeds--> RUNNING --request_stop--> STOPPING --Run done--> STOPPED
  |                           |                         |
  +--request_stop-----------> STOPPED                  +--Run error--> FAILED
  +--start failure----------> FAILED
                              +--Run/Machine error----> FAILED
```

`FAILED` and `STOPPED` are terminal. There is no restart. A second `start`
returns `ALREADY_STARTED` while running, `STOPPING`, `STOPPED`, or `FAILED`
according to current state. `request_stop` returns `OK` for the first transition
from `START` or `RUNNING`, and returns the exact terminal/current result on
repetition.

Actor admission is linearized by an Actor gate mutex around lifecycle
classification and `cflow_machine_instance_try_send`. The lock order is Actor
gate before Machine/Mailbox; no callback is invoked while the Actor gate is
held. The result mapping is exact:

| Actor/Mailbox observation | Actor send result |
| --- | --- |
| live ref, `RUNNING`, Mailbox `OK` | `ACCEPTED` |
| malformed ref/event or Mailbox invalid argument | `INVALID_ARGUMENT` |
| known Event with wrong descriptor | `TYPE_MISMATCH` |
| bounded Mailbox full | `FULL` |
| `START` | `NOT_STARTED` |
| `STOPPING` | `STOPPING` |
| `STOPPED` | `STOPPED` |
| `FAILED` | `FAILED` |
| owner destruction has begun | `STALE` |

The Actor does not retry, block, silently drop, overwrite, resize, or allocate
on the send path. A successful send contributes exactly one Mailbox admission.
Global FIFO is the existing Mailbox mutex-commit order, including concurrent
producers and self-send.

## Start, Execution, Failure, and Stop

`start` attaches the Machine instance as a Source, opens the identity Graph in
the existing `cflow_run`, enters `RUNNING`, and requests `SIZE_MAX` downstream
demand. It creates no Actor thread and no Actor-specific pump. The Machine's
SerialExecutor remains the sole transition serializer; the borrowed concurrent
Scheduler remains the Run dispatcher.

The Actor sink bridge forwards values to the optional user `on_value`. A false
return follows existing Run semantics and becomes the first Actor failure. The
bridge records the first error in owned storage, changes the state to `FAILED`,
signals waiters, and only then invokes the optional user `on_error`. On normal
Run completion it changes `STOPPING` to `STOPPED`, signals waiters, then invokes
the optional user `on_done`. User callbacks run without the Actor gate held.
Self-send and `request_stop` from callbacks are therefore permitted; destroy
and wait from callbacks are not.

An Event with a declared payload type but no enabled Machine transition is an
unhandled Event. Existing Machine semantics report an error; the Actor exposes
that as `FAILED`, retains the first error, rejects future work, and cancels all
remaining queued Events. Guard/action failures follow the same path. No
fallback transition exists.

The first stop transition changes Actor state before closing the Machine, so no
later send can be admitted. Machine close rejects new work, discards queued
Events, and permits an already executing transition to commit once. The Run
then reaches its terminal callback. Stopping from `START` closes the Machine
and moves directly to `STOPPED`, because no Run exists. Destruction performs
the same admission barrier, closes the Run synchronously, then destroys the
Machine and Graph. These are control-plane operations and require owner-side
serialization.

Accepted Events therefore reach exactly one of two terminal outcomes already
counted by Machine stats: completed, or cancelled during stop/failure. A
currently executing transition can complete exactly once; its result cannot be
committed twice.

## Boundedness and Observability

The Actor adds a fixed-size control block, one identity Graph, and one first
error allocation at most. Steady-state send and transition paths inherit the
fixed Mailbox payload arena and do not allocate. Capacity is the required,
non-zero `machine.mailbox_capacity`; `FULL` is the explicit backpressure result.
Reserved payload bytes, pending/in-flight counts, accepted/completed/failed/
cancelled counts, and state ID remain available through embedded Machine stats.
Actor stats add lifecycle rejection counters.

No duplicate state source is introduced: Machine state and Machine stats remain
authoritative for transition data, while the Actor lifecycle state alone owns
admission availability and terminal Actor status.

## Verification

C tests cover initialization and scheduler rejection; exact send results;
bounded saturation; concurrent producers; FIFO processing; self-send and stop
reentrancy; queued stop with a single in-flight commit; unhandled Event and
guard/action/sink failure; stale refs; deterministic replay; repeated lifecycle
calls; and C++ aggregate-header compatibility. Stress repeats concurrent send,
stop, and stale-ref races without unbounded waits.

Lean adds a pure Actor lifecycle layered over the existing Mailbox and Machine
models. It proves validity preservation, admission-only-while-running,
single-append acceptance, stop rejection/cancellation, terminal-state
stability, and a handoff/refinement theorem connecting Actor admission through
Mailbox receive to the same Machine event. Executable examples cover exact
statuses and deterministic replay. The formal model must contain no `sorry`,
`admit`, or new axioms.

Verification proceeds from focused Release tests to adjacent CFlow regression,
ASan where supported, deterministic stress, Lean `lake build`/`lake test`, and
generated-file checks. Lean build and test run sequentially because they share
`.lake/build`.
