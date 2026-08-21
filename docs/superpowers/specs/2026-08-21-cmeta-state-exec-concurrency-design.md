# CMeta State + Exec Concurrency Architecture

Status: architecture baseline  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`

## 1. Purpose

This specification defines two first-party modules built on CMeta Core:

```text
CMeta State
CMeta Exec
```

They are not part of the minimal semantic kernel and they do not depend on CMeta Extend.

CMeta Extend may later provide source syntax for `machine`, `Task<T>`, `async`, `await`, `spawn` and related constructs, but all runtime and validation semantics remain owned by State or Exec.

The design combines state machines, structured asynchronous execution, stackful coroutine backends and cross-platform event loops without conflating them.

## 2. Architectural position

```text
                     CMeta Extend
                    optional syntax
                         │
                         ▼
                     lowering
                         │
       ┌─────────────────┴─────────────────┐
       ▼                                   ▼
  CMeta State                          CMeta Exec
       ▲                                   ▲
       └────────────── CMeta Core ─────────┘
                                           │
                              ┌────────────┴────────────┐
                              ▼                         ▼
                      CoroutineBackend             Platform ports
                              │                         │
                       minicoro adapter         epoll/IOCP/kqueue

CFlow -> CMeta Core + selected CMeta Exec primitives
```

State and Exec share Core types/callables/contracts but are separate semantic domains.

## 3. Three state spaces MUST remain distinct

### Application state

Examples:

```text
Disconnected
Connecting
Connected
Closing
Closed
```

Owned by CMeta State.

### Task execution state

```text
NEW
RUNNABLE
RUNNING
WAITING
DONE
FAILED
CANCELLED
```

Owned by CMeta Exec.

### Executor placement

Which executor/thread owns a task and receives its wakeups.

A `Connected` machine may have a task in `WAITING`. Waking the task does not change the application machine state. Moving/posting work between executors does not create application state transitions.

## 4. CMeta State responsibilities

State owns:

```text
StateId
EventId
MachineDesc
EventDesc
TransitionDesc
serialized event processing
guard/action admission
determinism
reachability/dead-state analysis
runtime dispatch
```

State depends on Core:

```text
CType
Callable
Traits
Effect / Property / Contract
finite graph algorithms
reflection
```

State MUST NOT depend on:

```text
CMeta Extend parser
minicoro
native pollers
CFlow graph model
```

## 5. CMeta Exec responsibilities

Exec owns:

```text
Step
Resumable
Waitable
Waker
Task<T> semantic/runtime model
Executor
CancelToken
Scope
Coordination
Timer waitables
```

Exec depends on Core type/callable/contracts but not on concrete coroutine or OS implementations.

Exec MUST NOT expose minicoro, epoll, IOCP or kqueue types in its public API.

## 6. Existing CFlow execution semantics to extract

CFlow already contains general execution concepts:

```text
VALUE
VALUE_AND_DONE
WAIT
DONE
ERROR
Waker
Waitable
Resumable
Scheduler
ALL / ALL_DONE / ANY / LATEST / SEQUENCE
```

These concepts are broader than value-flow and may migrate into Exec when their semantics are truly generic.

Migration is semantic extraction, not redesign. Existing CFlow conformance remains mandatory during each step.

CFlow-specific graph/result/error policies remain in CFlow.

## 7. Exec Step

Exec preserves the current resumable vocabulary:

```c
typedef enum cmeta_step_kind {
    CMETA_STEP_VALUE,
    CMETA_STEP_VALUE_AND_DONE,
    CMETA_STEP_WAIT,
    CMETA_STEP_DONE,
    CMETA_STEP_ERROR
} cmeta_step_kind;

typedef struct cmeta_step {
    cmeta_step_kind kind;
    cmeta_waitable waitable;
    const char *error;
} cmeta_step;
```

`CMETA_STEP_ERROR` represents runtime/infrastructure failure, not an application-domain `Result<T,E>` error.

## 8. Waker and Waitable

```c
typedef struct cmeta_waker {
    void (*wake)(void *user);
    void *user;
} cmeta_waker;
```

A Waker may be called from another thread or an OS callback, but MUST NOT directly resume a coroutine on that foreign thread.

It requests/posts progress to the owning executor.

A Waitable represents one blocking condition:

```text
timer expiry
socket readiness
IO completion
child Task completion
coordination readiness
future channel availability
```

Conceptual interface:

```c
#define CMETA_WAITABLE_METHODS(X,I) \
    X(I,R1,bool,arm,cmeta_waker,waker) \
    X(I,V0,void,cancel,_)
CMETA_INTERFACE(cmeta_waitable, CMETA_WAITABLE_METHODS);
```

Cancellation/destruction MUST detach future wake delivery before referenced task storage is freed.

## 9. Resumable

```c
typedef struct cmeta_resume_ctx {
    cmeta_executor *executor;
    cmeta_cancel_token *cancel;
} cmeta_resume_ctx;

typedef struct cmeta_resumable_ops {
    cmeta_step (*resume)(void *state, cmeta_resume_ctx *ctx, void *out_value);
    void (*cancel)(void *state);
    void (*destroy)(void *state);
} cmeta_resumable_ops;

typedef struct cmeta_resumable {
    const char *name;
    const cmeta_type_desc *output_type;
    const cmeta_resumable_ops *ops;
    void *state;
} cmeta_resumable;
```

`Resumable` is a Bridge abstraction over execution implementations.

Possible implementations:

```text
CFlow source/generator/relation
minicoro-backed Task body
generated stackless machine
manual test resumable
```

Executor logic MUST depend on Resumable semantics, not on the concrete implementation strategy.

## 10. Task<T> semantic model

`Task<T>` is owned by Exec as a generic constructor and runtime abstraction.

CMeta Core supplies GenericConstructor/TypeId mechanics; Exec supplies Task meaning.

Conceptually:

```text
Task<T>
=
execution identity
+ eventual typed completion T
+ lifecycle state
+ cancellation
+ executor affinity
+ awaitability
```

Task is not identical to coroutine.

Coroutine is one Resumable strategy for executing a Task body.

## 11. Task handle and runtime object

A public typed task value SHOULD be a small handle to a stable-address runtime object rather than a copyable structure containing scheduler/coroutine state.

Conceptually:

```c
typedef struct cmeta_task_User {
    struct cmeta_task_User_impl *impl;
} cmeta_task_User;

typedef struct cmeta_task_User_impl {
    cmeta_task_base base;
    User result;
} cmeta_task_User_impl;
```

The exact generated spelling is not normative.

Successful result storage is typed and correctly aligned. Public successful-result APIs SHOULD NOT expose an untyped `void *` as the normal user-facing representation.

## 12. Task state machine

```c
typedef enum cmeta_task_state {
    CMETA_TASK_NEW,
    CMETA_TASK_RUNNABLE,
    CMETA_TASK_RUNNING,
    CMETA_TASK_WAITING,
    CMETA_TASK_DONE,
    CMETA_TASK_FAILED,
    CMETA_TASK_CANCELLED
} cmeta_task_state;
```

Allowed transitions:

```text
NEW       -> RUNNABLE
RUNNABLE  -> RUNNING
RUNNING   -> RUNNABLE      yield
RUNNING   -> WAITING       await
RUNNING   -> DONE
RUNNING   -> FAILED
RUNNING   -> CANCELLED
WAITING   -> RUNNABLE      wake
WAITING   -> CANCELLED     cancellation cleanup path
```

`DONE`, `FAILED` and `CANCELLED` are terminal.

A terminal Task MUST NOT be resumed again.

## 13. DONE vs application failure

Application errors and runtime failures are separate.

Example:

```text
Task<Result<User,HttpError>> = DONE(Err(HttpError))
```

is a normally completed Task carrying an application error value.

`CMETA_TASK_FAILED` is reserved for runtime/infrastructure failure such as an invalid backend state or executor/internal allocation failure.

This separation MUST be preserved in Extend diagnostics and future async syntax.

## 14. Task ownership and Scope

v1 structured tasks SHOULD belong to a Scope by default.

Scope invariant:

> Scope completion implies every scoped child is terminal.

Rules:

- child inherits cancellation lineage;
- default child runs on the scope owner's executor;
- an explicitly selected executor is fixed at task creation;
- normal scope exit joins children;
- cancelled/failed exit requests child cancellation and joins cleanup;
- scoped spawn has no detached mode.

This permits v1 to avoid making shared atomic reference counting the universal Task lifetime mechanism.

A future detached/shared task facility must be separate and explicit.

## 15. yield vs await

They are different Exec primitives.

### yield

```text
RUNNING -> RUNNABLE
```

No external dependency is registered. The task voluntarily returns scheduling control.

### await

```text
RUNNING
 -> arm Waitable
 -> WAITING
 -> suspend execution implementation
 -> Waker posts owner executor
 -> RUNNABLE
```

The coroutine backend provides call-stack suspension. Exec owns wait ownership, lifecycle transition and affinity rules.

## 16. Task as Waitable

Task completion SHOULD be exposable as a Waitable.

This makes the following share one waiting abstraction:

```text
Timer
I/O readiness/completion
Task completion
Channel readiness
Coordination readiness
```

Awaiting `Task<T>` then adds typed result extraction after completion.

## 17. Cancellation

Cancellation is cooperative in v1.

Conceptual API:

```c
typedef struct cmeta_cancel_token cmeta_cancel_token;

bool cmeta_cancel_requested(const cmeta_cancel_token *token);
void cmeta_cancel_request(cmeta_cancel_token *token);
```

Required cancellation checkpoints:

```text
before suspend
immediately after resume
explicit cancellation checkpoint
timer wait
coordination/join wait
future channel wait
```

Cancelling a waiting task performs:

```text
request cancellation
 -> cancel/detach active Waitable
 -> post owner Executor
 -> Task observes cancellation
 -> backend/task cleanup on owner Executor
 -> CANCELLED
```

OS thread cancellation is not used.

## 18. Coordination

Generic child execution coordination belongs to Exec.

```c
typedef enum cmeta_coord_mode {
    CMETA_COORD_ALL,
    CMETA_COORD_ALL_DONE,
    CMETA_COORD_ANY,
    CMETA_COORD_LATEST,
    CMETA_COORD_SEQUENCE
} cmeta_coord_mode;
```

This is a Composite + Policy design.

Mappings:

```text
ALL / ALL_DONE       join/all semantics
ANY                  race
LATEST               combine-latest consumers
SEQUENCE             ordered sequence/fallback
```

CFlow may compose these with its own relation result/error policies.

## 19. Coroutine port

Exec defines a backend-neutral Bridge port whose only concern is preserving/restoring an execution context.

Conceptually:

```c
typedef enum cmeta_coro_state {
    CMETA_CORO_SUSPENDED,
    CMETA_CORO_RUNNING,
    CMETA_CORO_DEAD,
    CMETA_CORO_ERROR
} cmeta_coro_state;

typedef struct cmeta_coro cmeta_coro;

typedef struct cmeta_coro_ops {
    bool (*create)(cmeta_coro *, const cmeta_coro_desc *);
    cmeta_coro_state (*state)(const cmeta_coro *);
    bool (*resume)(cmeta_coro *);
    bool (*yield)(cmeta_coro *);
    void (*destroy)(cmeta_coro *);
} cmeta_coro_ops;
```

Exact ABI may evolve, but Task/Exec semantics MUST NOT require minicoro-specific types.

## 20. minicoro adapter

`CMetaCoroMinicoro` implements the coroutine port.

Rules:

- includes `minicoro.h` only in adapter/private files;
- maps create/resume/yield/status/destroy;
- uses CMeta task/frame state as user data;
- does not use minicoro push/pop storage as typed Task transport;
- converts backend errors at the adapter boundary;
- does not redefine Task state semantics.

minicoro is an implementation strategy, not a semantic dependency.

## 21. Executor

Exec uses a Reactor/Proactor-compatible executor abstraction.

A native event-loop executor owns:

```text
ready queue
cross-thread post queue
timer queue
poll/completion port
owner-thread identity
shutdown state
```

Conceptual loop:

```text
drain posts
run bounded ready quantum
fire timers
compute wait timeout
poll readiness/completions
convert events to wake/posts
repeat
```

Ready execution MUST be bounded sufficiently to avoid starving timer/I/O progress.

## 22. Executor affinity

v1 tasks are pinned to one executor from creation through terminal cleanup.

A foreign thread may post/wake that executor. It MUST NOT directly resume the task's coroutine/backend implementation.

v1 excludes transparent coroutine migration and work stealing.

## 23. Deterministic executor

Exec SHOULD provide a deterministic/manual-clock executor with no native poller dependency.

This executor is the reference implementation for:

```text
unit tests
state transition tests
coordination tests
C-to-Lean conformance
failure/cancellation scheduling tests
```

Native execution is an adapter, not the only way to exercise Exec semantics.

## 24. Platform ports

Native integration is separated into small interfaces:

```text
ThreadOps
ClockOps
PollerOps / CompletionOps
WakeupOps
```

A single god `PlatformOps` interface SHOULD NOT be introduced.

Possible mappings:

```text
Linux       epoll + eventfd
Windows     IOCP
Darwin/BSD  kqueue
POSIX       poll fallback
```

Linux/Darwin are primarily readiness/Reactor oriented; IOCP is completion/Proactor oriented. Exec's port MUST be abstract enough not to force IOCP into a fake file-descriptor-readiness model.

## 25. State Machine model

CMeta State uses a table-driven finite state machine, not a GoF state-object hierarchy.

Conceptually:

```c
typedef uint32_t cmeta_state_id;
typedef uint32_t cmeta_event_id;

typedef struct cmeta_transition_desc {
    cmeta_state_id source;
    cmeta_event_id event;
    cmeta_state_id target;
    cmeta_callable guard;
    bool has_guard;
    cmeta_callable action;
    bool has_action;
} cmeta_transition_desc;
```

Machine descriptors contain finite state/event/transition tables and exact CType metadata for context and event payloads.

## 26. Guard and action as Command

State reuses Core `cmeta_callable` as the Command abstraction.

It MUST NOT introduce a second unrelated callback type system.

For a machine context `Context` and event payload `Payload`, generated finite callable signatures conceptually represent:

```text
guard  : (Context*, Payload) -> bool
action : (Context*, Payload) -> bool
```

Guard requirements:

```text
PURE
DETERMINISTIC
TOTAL
```

Stateful/IO/MAY_FAIL/UNKNOWN guards are invalid.

Actions may be stateful, IO-bearing or MAY_FAIL according to declared contracts, but a v1 transition action does not suspend.

## 27. State determinism

v1 permits at most one transition for each `(state,event)` pair.

This avoids pretending Core can prove arbitrary guard mutual exclusion.

Future ordered competing guards require a separate explicit policy and formal semantics.

## 28. State dispatch semantics

One event dispatch is serialized per machine instance.

Exact order:

```text
validate current state/event/payload CType
 -> locate unique transition
 -> no transition: UNHANDLED
 -> evaluate guard if present
 -> guard false: REJECTED
 -> run action if present
 -> action false: ACTION_ERROR; source state retained
 -> commit target state
 -> SUCCESS
```

There are no enter/exit hooks in v1.

Application-level failures SHOULD be typed events/data rather than infrastructure action errors.

## 29. State + Task integration

A state transition does not suspend.

Asynchronous activity is modeled as:

```text
State transition
 -> action starts Task
 -> action returns
 -> Task executes independently
 -> completion enqueues typed Event
 -> next machine transition consumes Event
```

Example:

```text
Disconnected + Open(Address)
    -> Connecting
    action: spawn connect Task

Task completes Ok(Socket)
    -> enqueue Connected(Socket)

Connecting + Connected(Socket)
    -> Connected
```

This keeps logical state durable and activity temporary.

Task completion MUST NOT mutate machine state directly from a foreign thread.

## 30. Extend ownership of syntax

The following are Extend concerns only:

```text
machine
state
event
on
when
->
Task<T> angle-bracket spelling
async
await
spawn
scope
```

State/Exec public semantic APIs MUST remain usable from strict C11 without the parser.

Extend may lower:

```text
machine syntax -> MachineDesc/EventDesc/TransitionDesc
Task<T>        -> Exec-owned generic application
await          -> Exec await primitive
async fn       -> Task/Resumable construction
```

Extend MUST NOT redefine State/Exec invariants.

## 31. CFlow relationship

CFlow remains a separate value-flow consumer/domain.

It may reuse Exec:

```text
Resumable
Waitable
Waker
Executor
Coordination
```

but keeps:

```text
CFlow Graph IR
operators
relations
result policies
optimizer
plan compiler
runtime value-flow semantics
```

State, Exec and CFlow are not one universal graph representation.

## 32. Formal verification split

State proofs:

```text
transition target validity
determinism
step preservation
reachability/dead states
guard/action signature admission
```

Exec proofs:

```text
legal Task lifecycle transitions
terminal-state immutability
WAIT -> wake -> RUNNABLE legality
coordination semantics
executor-affinity invariants
cancellation progression
```

Extend proofs/conformance later establish that syntax lowering preserves these module semantics.

Native/minicoro conformance is executable refinement evidence at adapter boundaries.

## 33. v1 exclusions

v1 explicitly excludes:

```text
work stealing
transparent coroutine migration
preemptive cancellation
detached scoped children
borrow checker / ownership language
hierarchical statecharts
parallel statechart regions
state history
suspending inside state transition actions
actor framework
transparent blocking syscall rewriting
WASM native acceptance target
```

These are architectural scope limits, not placeholders.

## 34. Migration sequence

Recommended order:

1. extract generic CFlow execution primitives into Exec-compatible interfaces while preserving behavior;
2. establish deterministic Exec tests before native backends;
3. define Task lifecycle/typed completion and Scope;
4. add coroutine port and minicoro adapter;
5. add native executor adapters;
6. implement State independently on Core;
7. integrate State action -> Task -> typed Event flow;
8. add Extend syntax only after semantic modules are stable.

Parser syntax is intentionally late because it must lower to already valid semantic APIs.

## 35. Acceptance criteria

The design is correct when:

1. State and Exec build without CMeta Extend;
2. Exec builds without minicoro/native pollers;
3. deterministic Exec tests exercise Task/Wait/Cancel/Coordination semantics;
4. minicoro can be replaced without changing Task API;
5. native platform code is isolated behind ports;
6. State guards/actions reuse Core Callable/contracts;
7. transition actions never suspend in v1;
8. Task completion can feed typed machine events without foreign-thread state mutation;
9. CFlow can progressively reuse generic Exec primitives without moving CFlow graph semantics into Exec.

This specification is subordinate to the CMeta Hexagonal Architecture and must preserve its dependency rules.
