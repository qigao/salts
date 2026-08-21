# CMeta State + Exec Concurrency Architecture

Status: draft for user review  
Date: 2026-08-21  
Branch: `leanv4`

## 1. Purpose

This specification defines a concurrency and state-machine substrate for CMeta that combines four concerns without conflating them:

1. finite typed application state machines;
2. resumable task execution and structured concurrency;
3. stackful coroutine execution, initially through a `minicoro.h` backend;
4. cross-platform executor, timer, thread, wakeup and I/O polling integration.

CMeta remains a finite typed generative layer over ordinary C11. This design does not add a borrow checker, GC, arbitrary compile-time execution, transparent syscall rewriting, or a new non-C ABI.

The central invariant is that three state spaces remain distinct:

- **application state** — protocol/UI/device/workflow state such as `Connected`, `Closing`, `Closed`;
- **task execution state** — `NEW`, `RUNNABLE`, `RUNNING`, `WAITING`, `DONE`, `FAILED`, `CANCELLED`;
- **executor placement** — the executor/thread that owns a task and receives its wakeups.

A `Connected` machine may own a task in `WAITING`. A task wakeup may change execution state without changing application state. Executor placement must never be encoded as an application-machine state.

## 2. Architectural decomposition

The architecture is split into independently testable modules and CMake targets:

```text
TurboUtils::CMeta                 existing CType/Traits/Callable/Schema core
        │
        ├── TurboUtils::CMetaState
        │      finite machine/event/transition IR
        │
        └── TurboUtils::CMetaExec
               Step/Waker/Waitable/Resumable
               Task/Scope/Cancel/Coordination
               deterministic executor
                    │
                    ├── TurboUtils::CMetaCoroMinicoro
                    │      stackful coroutine backend
                    │
                    └── TurboUtils::CMetaExecNative
                           native thread/timer/poller executor

TurboUtils::CFlow
        └── progressively consumes TurboUtils::CMetaExec
```

`CMetaState` and `CMetaExec` share CType, callable signatures, effects/properties and finite graph algorithms, but they are different IR domains.

- CFlow graph edges describe value-flow computations.
- State-machine edges describe event-triggered state transitions.
- Exec state describes whether a computation can currently run.

They must not be collapsed into one universal graph structure.

## 3. v1 goals

### 3.1 CMeta State

Provide:

- a finite state universe;
- a finite typed event universe;
- exact event payload CType metadata;
- deterministic transitions `(state,event) -> target`;
- optional typed guard;
- optional typed action;
- reachability/dead-state analysis;
- immutable reflection descriptors;
- table-driven execution;
- generated-switch backend later using the same descriptor semantics;
- Lean model plus real-C conformance snapshot.

### 3.2 CMeta Exec

Provide:

- `Step` with `VALUE`, `VALUE_AND_DONE`, `WAIT`, `DONE`, `ERROR`;
- `Waker`;
- `Waitable`;
- `Resumable`;
- `Executor`;
- generated typed `Task<T>` wrappers;
- cooperative `yield` and `await` core operations;
- `CancelToken`;
- structured `Scope`;
- generic coordination `ALL`, `ALL_DONE`, `ANY`, `LATEST`, `SEQUENCE`;
- timer waitables;
- deterministic/manual-clock executor;
- compatibility bridge for existing CFlow runtime users.

### 3.3 Coroutine backend

Provide a backend-neutral coroutine ABI. The first backend uses minicoro only to preserve/switch the C call stack. CMeta public Task/Exec headers must not expose minicoro types.

### 3.4 Native execution

Provide Linux, Windows and Darwin executor backends with the same public Exec API.

## 4. Explicit v1 non-goals

v1 excludes:

- work stealing;
- arbitrary task/coroutine migration between executor threads;
- preemptive cancellation;
- detached scoped children;
- hierarchical/parallel statecharts;
- state-history nodes;
- multiple competing transitions for the same `(state,event)`;
- suspending inside a state transition action;
- parser/frontend `async`, `await`, `machine` syntax;
- actor framework;
- transparent blocking-I/O conversion;
- WASM as an acceptance platform.

These are deliberate boundaries, not placeholders.

## 5. Existing CFlow semantics to extract, not redesign

CFlow already has the generic concepts needed by Exec:

- `cflow_step_kind` including `VALUE_AND_DONE`;
- `cflow_waker`;
- `cflow_waitable`;
- `cflow_resumable`;
- scheduler interface and deterministic test scheduler;
- coordination `ALL`, `ALL_DONE`, `ANY`, `LATEST`, `SEQUENCE`;
- relation execution that lowers a structured relation into a resumable.

The first Exec work is a semantic extraction. Existing CFlow behavior and current formal conformance remain mandatory during migration.

## 6. CMeta Exec primitive ABI

### 6.1 Step

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

`error` is borrowed runtime diagnostic text. It is not a typed application failure. Application errors remain ordinary typed data/events.

### 6.2 Waker

```c
typedef struct cmeta_waker {
    void (*wake)(void *user);
    void *user;
} cmeta_waker;
```

Required semantics:

- may be invoked from an OS callback or foreign thread;
- never directly resumes a stackful coroutine on that foreign thread;
- posts/marks work for the owning executor;
- redundant wakeups are tolerated but cannot cause duplicate terminal completion;
- cancellation/destruction detaches future wake delivery before task storage is freed.

### 6.3 Waitable

```c
#define CMETA_WAITABLE_METHODS(X,I) \
    X(I,R1,bool,arm,cmeta_waker,waker) \
    X(I,V0,void,cancel,_)
CMETA_INTERFACE(cmeta_waitable, CMETA_WAITABLE_METHODS);
```

A waitable represents one blocking reason: timer expiry, socket readiness, IOCP completion, channel readiness, child completion, or coordination readiness.

### 6.4 Resumable

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

`Resumable` is the common execution object. CFlow sources/generators/relations, coroutine tasks and later generated stackless machines may all implement it. Executors do not depend on the concrete backend.

## 7. Task model

### 7.1 State machine

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
RUNNING   -> RUNNABLE      cooperative yield
RUNNING   -> WAITING       await
RUNNING   -> DONE
RUNNING   -> FAILED
RUNNING   -> CANCELLED     cancellation observed
WAITING   -> RUNNABLE      wake
WAITING   -> CANCELLED     cancellation wake/cleanup path
```

`DONE`, `FAILED`, `CANCELLED` are terminal and cannot become runnable again.

### 7.2 Typed Task<T>

The runtime owns a generic base; CMeta generation creates concrete result storage.

```c
typedef struct cmeta_task_base {
    cmeta_task_state state;
    const cmeta_type_desc *result_type;
    cmeta_executor *executor;
    cmeta_resumable resumable;
    cmeta_waitable active_wait;
    cmeta_cancel_token *cancel;
    const char *runtime_error;
} cmeta_task_base;
```

Conceptually `Task<User>` lowers to:

```c
typedef struct cmeta_task_User {
    cmeta_task_base base;
    User result;
} cmeta_task_User;
```

Successful typed results do not travel through public `void *` APIs. Parser syntax `Task<User>` is outside v1; the generic-kind layer may later generate the same concrete wrapper.

## 8. yield and await

`yield` means voluntary scheduling relinquishment:

```text
RUNNING -> RUNNABLE
```

The owning executor requeues the same task.

`await` means dependency on a concrete waitable:

```text
RUNNING
   -> arm waitable
   -> WAITING
   -> backend suspends
   -> waker posts owner executor
   -> RUNNABLE
```

The coroutine backend supplies stack suspension only. Exec owns task-state transitions, wait ownership and executor affinity.

## 9. Cooperative cancellation

```c
typedef struct cmeta_cancel_token cmeta_cancel_token;

bool cmeta_cancel_requested(const cmeta_cancel_token *token);
void cmeta_cancel_request(cmeta_cancel_token *token);
```

Mandatory checkpoints:

- before an await suspends;
- immediately after an await resumes;
- explicit cancel point;
- timer wait;
- coordination/join wait;
- future channel send/recv wait.

Cancelling a waiting task performs this order:

```text
request cancellation
    -> cancel/detach active waitable
    -> post task owner executor
    -> task observes cancellation
    -> backend/task cleanup on owner executor
    -> CANCELLED
```

No OS thread cancellation is used.

## 10. Structured Scope

A Scope owns live child relationships.

Invariant:

> Scope exit cannot complete while a scoped child is non-terminal.

v1 rules:

- child inherits the scope cancellation lineage;
- default spawn creates the child on the scope owner's executor;
- an explicit executor is permitted only at child creation; the child is then pinned to that executor for its entire lifetime;
- normal scope exit joins all children;
- cancelled/failed scope exit requests child cancellation and joins terminal cleanup;
- scoped spawn has no detached mode.

A detached-task API, if ever added, is a separate non-scope facility.

## 11. Coordination

Move the generic coordination algebra below CFlow:

```c
typedef enum cmeta_coord_mode {
    CMETA_COORD_ALL,
    CMETA_COORD_ALL_DONE,
    CMETA_COORD_ANY,
    CMETA_COORD_LATEST,
    CMETA_COORD_SEQUENCE
} cmeta_coord_mode;
```

Mappings:

```text
ALL / ALL_DONE       join/all
ANY                  race
LATEST               combine-latest consumers
SEQUENCE             ordered sequence/fallback
```

CFlow keeps its relation-specific completion/result/error policy. Exec owns the child-resumable coordination mechanism. Existing CFlow coordination proofs should be generalized/reused rather than replaced.

## 12. Coroutine backend ABI

The backend answers only: how is the C call stack suspended and restored?

```c
typedef enum cmeta_coro_state {
    CMETA_CORO_SUSPENDED,
    CMETA_CORO_RUNNING,
    CMETA_CORO_DEAD,
    CMETA_CORO_ERROR
} cmeta_coro_state;

typedef struct cmeta_coro cmeta_coro;
typedef void (*cmeta_coro_entry_fn)(cmeta_coro *coro, void *user);

typedef struct cmeta_coro_desc {
    cmeta_coro_entry_fn entry;
    void *user;
    size_t stack_size;
    void *allocator_user;
} cmeta_coro_desc;

typedef struct cmeta_coro_ops {
    bool (*create)(cmeta_coro *coro, const cmeta_coro_desc *desc);
    cmeta_coro_state (*state)(const cmeta_coro *coro);
    bool (*resume)(cmeta_coro *coro);
    bool (*yield)(cmeta_coro *coro);
    void (*destroy)(cmeta_coro *coro);
} cmeta_coro_ops;
```

### minicoro backend rules

`TurboUtils::CMetaCoroMinicoro`:

- is optional and depends on `TurboUtils::CMetaExec`;
- includes `minicoro.h` only in backend/private files;
- maps create/resume/yield/status/destroy into the backend ABI;
- stores the CMeta Task/frame pointer as backend user data;
- does not use minicoro push/pop storage as typed Task transport;
- keeps typed result storage in CMeta-owned memory;
- translates minicoro failures to CMeta runtime errors at the backend boundary.

No public `cmeta/exec/*.h` header includes minicoro.

## 13. Executor and thread model

### 13.1 Affinity

A v1 task is pinned to one executor from creation to terminal cleanup.

A foreign thread may enqueue/wake the owning executor. It never calls coroutine resume directly.

### 13.2 Executor responsibilities

A native event-loop executor owns:

- owner thread identity;
- ready queue;
- cross-thread post queue;
- timer queue;
- platform poller;
- shutdown state.

Conceptual loop:

```text
drain cross-thread posts
run bounded ready-task quantum
fire expired timers
compute poll timeout
poll OS events/completions
convert completions into executor wake/posts
repeat
```

Ready-task execution is bounded per loop turn so I/O/timer progress cannot be starved by a permanently nonempty ready queue.

### 13.3 Deterministic executor

`TurboUtils::CMetaExec` includes a deterministic/manual-clock executor independent of native pollers. It is the reference executor for unit tests and C-to-Lean conformance witnesses.

### 13.4 Threading

v1 supports one event-loop executor per thread and explicit posting between executors. No work stealing or transparent migration is permitted.

## 14. Native platform layer

`TurboUtils::CMetaExecNative` depends on `TurboUtils::CMetaExec` and isolates OS code.

Interfaces are split by responsibility:

```text
ThreadOps
ClockOps
PollerOps
WakeupOps
```

Required monotonic clock:

```c
uint64_t cmeta_monotonic_ns(void);
```

Initial native mappings:

```text
Linux       epoll + eventfd
Windows     IOCP + executor completion wake
Darwin/BSD  kqueue + user-event/pipe wake
POSIX       poll + pipe fallback
```

Public Task/State/CFlow code contains no `_WIN32`, `__linux__` or Darwin branching. WASM is deferred to a separate design because browser event-loop/Asyncify constraints differ materially from native pollers.

## 15. State Machine IR

### 15.1 Finite descriptor

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

typedef struct cmeta_machine_desc {
    const char *name;
    const cmeta_type_desc *context_type;
    const cmeta_state_desc *states;
    size_t state_count;
    const cmeta_event_desc *events;
    size_t event_count;
    const cmeta_transition_desc *transitions;
    size_t transition_count;
    cmeta_state_id initial_state;
} cmeta_machine_desc;
```

Each event has exactly one payload CType. A payload-less event uses a registered `cmeta_unit` CType.

### 15.2 Exact guard/action ABI

State Core does **not** introduce a second callback type system.

For a machine with context type `Context` and event payload `Payload`, generation registers finite concrete CMeta signature rows for:

```text
guard  : (Context*, Payload) -> bool
action : (Context*, Payload) -> bool
```

`Context*`, `Payload` and `bool` are CTypes in the same finite signature universe used by `cmeta_callable`.

This creates one prerequisite for State Core:

> CMeta signature generation must admit registered user CTypes/pointer CTypes into a finite per-build signature schema.

It remains finite: only concrete context/payload combinations referenced by declared machines are generated. State does not add dynamic arbitrary-signature invocation.

Guard return meaning:

- `true`: transition may proceed;
- `false`: event is not accepted by this transition; state remains unchanged.

Action return meaning:

- `true`: action succeeded; commit target state;
- `false`: runtime action failure; state remains source state.

### 15.3 Guard contract

Every guard requires:

```text
PURE + DETERMINISTIC + TOTAL
```

IO, STATEFUL, MAY_FAIL or UNKNOWN guards are rejected by machine validation.

### 15.4 Action contract

Actions may carry STATEFUL, IO or MAY_FAIL effects. A v1 action never suspends. If asynchronous work is required, the action spawns a Task and returns; completion emits a later typed event.

### 15.5 Determinism

v1 permits **at most one transition per `(state,event)`**.

This deliberately excludes ordered competing guards because CMeta cannot generally prove arbitrary guard mutual exclusion. The schema-level determinism rule is decidable and directly formalizable.

## 16. State runtime semantics

Machine event processing is serialized on the machine owner executor.

Dispatch order is exact:

```text
validate machine/current state/event/payload CType
    -> locate unique transition
    -> no transition: UNHANDLED, state unchanged
    -> evaluate guard if present
    -> guard false: REJECTED, state unchanged
    -> run action if present
    -> action false: ACTION_ERROR, state unchanged
    -> commit target state
    -> SUCCESS
```

There are no enter/exit hooks in v1.

Application failures intentionally represented by the protocol should be events/typed data. `ACTION_ERROR` is an infrastructure/action failure signal.

## 17. State + Task integration

A transition does not suspend.

Example:

```text
Disconnected + Open(Address)
    -> Connecting
    action: spawn connect Task

Task completes Ok(Socket)
    -> enqueue Connected(Socket) event

Connecting + Connected(Socket)
    -> Connected
```

Task completion never mutates machine state directly from a foreign thread. It posts a typed event to the machine owner executor, which serializes dispatch.

This pattern can later support:

```text
Actor = Machine + typed Mailbox<Event> + Executor affinity
```

Actor is not part of v1.

## 18. Channel<T> compatibility requirement

Channel is not required before Task/Executor, but Exec must support it without ABI redesign.

Future typed operations conceptually provide:

```text
send(Channel<T>, T)
recv(Channel<T>) -> typed waiting Task/value
```

Cross-thread send enqueues data and wakes/posts the receiver executor; it never resumes a coroutine on the sender thread.

## 19. CFlow migration

Migration is staged.

### A. Extract compatibility primitives

Add CMeta Exec equivalents of:

- Step;
- Waker;
- Waitable;
- Resumable;
- deterministic executor/scheduler concepts;
- coordination.

Keep CFlow compatibility typedefs/wrappers where practical.

### B. Adopt internally

Move/adapt generic execution responsibilities currently in:

```text
cflow/runtime.c
cflow/coord.c
cflow/subrun.c
cflow/scheduler.c
cflow/scheduler_worker.c
```

to consume CMeta Exec.

CFlow graph/lowering/optimization/plan semantics stay in CFlow.

### C. Remove duplication only after equivalence

Current CFlow unit tests, runtime differential witnesses, structured coordination witnesses and Lean conformance remain green throughout extraction. Public CFlow symbol removal/deprecation is a separate later decision.

## 20. Effects and properties

Concurrency reuses the existing CMeta contract system:

```text
PURE
STATEFUL
ASYNC
IO
MAY_FAIL
UNKNOWN

DETERMINISTIC
TOTAL
IDEMPOTENT
NO_ALIAS
ASSOCIATIVE
```

Rules:

- async task producers normally carry ASYNC;
- native I/O task producers carry ASYNC | IO;
- State guards require PURE + DETERMINISTIC + TOTAL;
- State actions declare their real effects;
- later parallelization eligibility may require PURE + DETERMINISTIC + TOTAL;
- optimizers continue to consume declared contracts only.

v1 does not invent `SEND`, `SYNC` or `THREAD_SAFE` traits. Executor affinity is an explicit runtime ownership rule.

## 21. Ownership and lifetime

This is explicit C ownership, not language ownership inference.

Normative ownership rules:

- task owns its resumable/backend state and typed result storage;
- executor owns scheduling references while a task is queued/running;
- scope owns child-lifetime relationships;
- coordination owns successfully moved child resumables;
- armed waitable owns or safely references its registration until fire/cancel;
- active waitable is detached/cancelled before task destruction;
- terminal task cleanup occurs on its owner executor;
- machine descriptor metadata is immutable;
- machine instance owns mutable context/current state;
- event payload ownership is defined by the generated event API/caller contract, not inferred by the state engine.

## 22. Error classes

Keep three classes distinct:

1. **runtime infrastructure error** — allocation failure, invalid resumable, poll registration failure; represented by `CMETA_STEP_ERROR`/task runtime failure;
2. **typed application error** — ordinary result/event data such as connection failure;
3. **cancellation** — task terminal state, not automatically application error.

This separation is required for structured concurrency and machine reasoning.

## 23. Formal verification boundaries

Formal verification is layered.

### State Core

Prove/check:

- state/event identifiers are in finite universes;
- transition source/target validity;
- exact event payload CType preservation;
- unique `(state,event)` transition determinism;
- guard/action signature admission;
- successful dispatch commits the statically known target;
- failed guard/action leaves source state unchanged;
- finite reachability/dead-state analysis.

### Task Core

Prove/check:

- terminal tasks cannot resume;
- only RUNNABLE becomes RUNNING;
- WAITING becomes RUNNABLE through wake/cancellation path;
- successful completion carries declared result CType;
- cancellation cannot fabricate successful typed output;
- scope terminality implies all scoped children terminal.

### Coordination

Generalize/reuse current executable proofs for ALL, ALL_DONE, ANY, LATEST and SEQUENCE.

### minicoro boundary

Do not prove minicoro internals. Prove/validate the refinement trace:

```text
CMeta resume
 -> backend resume
 -> backend yield/completion
 -> CMeta task transition
```

A deterministic C witness generates Lean snapshots for create/resume/yield/wait/wake/complete/cancel/destroy traces.

### Platform boundary

Formal claims stop at Waitable/Waker/Executor contracts. OS fairness and kernel implementation correctness are not claimed.

## 24. Testing strategy

Use CTest for C/CMeta behavior and retain `lake build --wfail` for formal proofs.

State tests cover:

- invalid source/target/event;
- payload mismatch;
- duplicate `(state,event)` rejection;
- impure guard rejection;
- guard false leaves source state;
- action false leaves source state;
- successful transition commits target;
- reachability/dead-state calculation;
- table/generated backend equivalence once both exist.

Exec tests cover:

- legal/illegal task transitions;
- yield requeue;
- wait/wake;
- foreign-thread wake posts rather than resumes directly;
- cancel runnable task;
- cancel waiting task;
- terminal immutability;
- scope child join/cancellation.

Coroutine backend tests cover:

- stack locals survive yield/resume;
- nested C call depth can suspend through backend abstraction;
- typed result survives completion;
- terminal task rejects later resume;
- cancellation cleanup runs on owner executor.

Native executor tests cover:

- monotonic timer;
- executor self-wake;
- cross-thread post;
- timer waitable;
- platform poller smoke test.

Real implementation conformance follows the established pattern:

```text
real C implementation
 -> deterministic generated Lean snapshot
 -> Lean checker/theorem
```

Separate State, Task and coroutine generators are preferred so failures isolate one layer.

## 25. Platform CI

Acceptance matrix for native Exec eventually includes:

```text
Linux
Windows
macOS
```

Android/iOS follow native desktop stabilization. WASM remains a separate design.

## 26. Proposed source layout

```text
cmeta/
  include/cmeta/
    state/
      machine.h
      event.h
      transition.h
      validate.h
      exec.h
    exec/
      step.h
      waker.h
      waitable.h
      resumable.h
      executor.h
      task.h
      cancel.h
      scope.h
      coord.h
    coro/
      backend.h
    platform/
      thread.h
      clock.h
      poller.h

  src/
    state/
      validate.c
      exec.c
    exec/
      executor.c
      task.c
      cancel.c
      scope.c
      coord.c
    coro/
      minicoro_backend.c
    platform/
      linux/...
      windows/...
      darwin/...
      posix/...
```

CMake builds separate targets named in Section 2 rather than expanding `src/cmeta.c` into a catch-all runtime file.

## 27. Implementation subprojects

This umbrella spec is intentionally implemented as four separately planned/accepted projects.

### 1. CMeta State Core

Deliver finite descriptors, user-CType signature generation required by machine callbacks, validation, deterministic dispatch, reachability, CTest and Lean conformance. No thread/coroutine dependency.

### 2. CMeta Exec Core

Deliver Step/Waker/Waitable/Resumable extraction, deterministic executor, Task state model, cancellation, Scope, coordination and CFlow compatibility adapters. No minicoro/native poller required.

### 3. minicoro Backend

Deliver backend ABI implementation, stackful Task runner, yield/await bridge, cancellation cleanup and C-to-Lean lifecycle trace. Deterministic executor/synthetic waitables are sufficient for acceptance.

### 4. Native Platform Executor

Deliver Linux epoll/eventfd, Windows IOCP and Darwin kqueue executors, timers, cross-thread posting and Linux/Windows/macOS CI.

Each subproject must be green and usable before the next relies on it.

## 28. Acceptance criteria

The architecture is implemented when:

1. CMeta State declares, validates, reflects and executes a flat typed machine without CFlow.
2. Machine callbacks use the existing finite CMeta signature/callable model, including registered user CTypes, rather than a second callback type system.
3. CMeta Exec runs `Task<T>` with the deterministic executor without minicoro.
4. The same Task abstraction runs through minicoro without changing Task API or result ABI.
5. A waiting task resumes only after its waker posts the owner executor.
6. A cancelled waiting task detaches its waitable and terminates on the owner executor.
7. Scope exit implies all scoped children terminal.
8. Task completion integrates with State through typed queued events, not foreign-thread state mutation.
9. CFlow consumes CMeta Exec primitives while all existing CFlow runtime/formal conformance remains green.
10. Linux, Windows and macOS native backends expose the same Exec API.
11. Lean proves the finite State and Task state-transition invariants, with generated real-C witnesses connecting implementation behavior to the models.

## 29. Summary invariant

The design assigns one question to each layer:

```text
CMeta State:
  What logical state is the system in, and which typed event may change it?

CMeta Exec:
  What computation is runnable, waiting, complete, failed or cancelled?

Executor/Thread:
  Where and when may that computation resume?

Coroutine backend:
  How is the C call stack suspended/restored?

Platform:
  How does this OS report time, readiness and completion?
```

No layer is allowed to answer another layer's question implicitly. That separation is the primary architectural guarantee.