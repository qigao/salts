# CMeta State + Exec Concurrency Architecture

Status: draft for user review
Date: 2026-08-21
Branch: `leanv4`

## 1. Purpose

This specification defines a concurrency and state-machine substrate for CMeta that combines four concerns without conflating them:

1. logical application state machines;
2. resumable task execution and structured concurrency;
3. stackful coroutine execution, initially through a `minicoro.h` backend;
4. cross-platform executor, timer, thread, wakeup and I/O polling integration.

The design keeps CMeta as a finite typed generative layer over ordinary C11. It does not turn CMeta into a replacement systems language, does not add ownership or borrow semantics, and does not require a new runtime ABI for ordinary C code.

The core architectural rule is that three state spaces remain distinct:

- **application state**: protocol, UI, device or workflow state, such as `Connected`, `Closing`, `Closed`;
- **task execution state**: `NEW`, `RUNNABLE`, `RUNNING`, `WAITING`, `DONE`, `FAILED`, `CANCELLED`;
- **executor placement state**: which executor/thread owns a task and where a wakeup must be posted.

A task waiting for socket readability may still belong to an application state `Connected`. A task changing thread/executor placement must not silently change application state. The implementation and formal model must preserve this separation.

## 2. Goals

The design must provide the following capabilities.

### 2.1 CMeta State

A finite typed state-machine IR with:

- explicit finite state set;
- typed events with optional payloads;
- transitions from `(state,event)` to `target state`;
- optional typed guards;
- optional typed actions;
- deterministic transition validation;
- reachability analysis;
- table-driven and generated-switch execution backends;
- reflection metadata suitable for debugging, serialization and formal conformance.

### 2.2 CMeta Exec

A reusable execution substrate with:

- `Resumable`;
- `Step` = `VALUE`, `WAIT`, `DONE`, `ERROR`;
- `Waitable`;
- `Waker`;
- `Executor`;
- typed `Task<T>` descriptors;
- cooperative `yield`;
- typed `await` semantics;
- cancellation tokens;
- structured `Scope` lifetime;
- reusable coordination primitives (`ALL`, `ALL_DONE`, `ANY`, `LATEST`, `SEQUENCE`);
- timer integration;
- typed channels as a later consumer of the same wait/wake substrate.

### 2.3 Coroutine backend

A backend-independent coroutine interface whose first implementation wraps `minicoro.h`. CMeta APIs must not expose `mco_coro`, `mco_desc`, `mco_yield` or any other minicoro-specific type.

### 2.4 Cross-platform execution

A platform layer that isolates OS differences for:

- thread identity and thread startup/join;
- monotonic time;
- executor wakeup;
- I/O poll registration and wait;
- platform-specific completion delivery.

The user-facing `Task`, `StateMachine`, `Waitable`, `Scope` and CFlow APIs must not contain platform preprocessor branches.

### 2.5 CFlow migration

CFlow currently owns general execution concepts including `cflow_resumable`, `cflow_waitable`, `cflow_waker`, `cflow_scheduler` and `cflow_coord_mode`. These concepts are broader than stream execution and must migrate downward into CMeta Exec while preserving CFlow behavior and current conformance tests.

## 3. Non-goals for v1

The first implementation explicitly excludes:

- work stealing;
- arbitrary coroutine migration between executor threads;
- preemptive cancellation;
- automatic lifetime inference;
- borrow checking;
- GC;
- transparent rewriting of blocking syscalls;
- hierarchical statecharts;
- parallel statechart regions;
- state history nodes;
- asynchronous transition actions that suspend the transition itself;
- compiler-owned `async/await` syntax;
- parser/frontend changes;
- a general actor framework.

These exclusions are architectural, not temporary missing implementation details. They keep the first verified subset small and make later extensions additive.

## 4. High-level architecture

```text
                         CMeta Core
                           │
          CType / Traits / Callable / Contract
                           │
              ┌────────────┴────────────┐
              │                         │
        CMeta State                 CMeta Exec
              │                         │
      machine/event/guard       resumable/waitable
      action/transition         task/scope/cancel
              │                 executor/coordination
              │                         │
              └──────────┬──────────────┘
                         │
                   consumers/backends
              ┌──────────┼───────────┐
              │          │           │
            CFlow      Task API    Channel<T>
                         │
                 Coroutine Backend
                         │
              ┌──────────┴──────────┐
              │                     │
          minicoro backend      future backend
              │
            Platform
              │
      Linux / Windows / Darwin / generic POSIX
```

CMeta State and CMeta Exec share CType, callable signatures, effects, properties, finite graph algorithms and generated metadata. They are otherwise separate IR domains.

CFlow remains a value-flow graph. State machines remain event-transition graphs. They must not be represented by the same graph node type merely because both are finite graphs.

## 5. Existing CFlow concepts to preserve

The migration must preserve the current runtime semantics:

```c
VALUE
VALUE_AND_DONE
WAIT
DONE
ERROR
```

The generic CMeta Exec form may initially retain `VALUE_AND_DONE` for compatibility, even if the public task API normally uses terminal completion separately.

Current CFlow semantics already establish useful contracts:

- a `Waitable` can be armed with a `Waker` and cancelled;
- a resumable returns a waitable when it cannot make progress;
- a scheduler is an interface rather than an inheritance hierarchy;
- coordination owns child resumables and exposes retained typed values;
- relation execution is implemented by lowering a structured relation into a resumable and driving it through the generic runtime.

The initial migration is semantic extraction, not redesign of these proven behaviors.

## 6. CMeta Exec primitive ABI

### 6.1 Step

Proposed public core shape:

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

`error` is borrowed diagnostic text. It is not the typed user result error. Typed application failures belong in `Result<T,E>`-like data or task completion metadata; runtime infrastructure failures use `CMETA_STEP_ERROR`.

### 6.2 Waker

```c
typedef struct cmeta_waker {
    void (*wake)(void *user);
    void *user;
} cmeta_waker;
```

Required semantics:

- `wake` may be called from a foreign OS callback or another thread;
- `wake` must not directly resume a coroutine on an arbitrary foreign thread;
- `wake` marks/posts work to the owning executor;
- redundant wakeups are permitted but must not cause duplicate task completion;
- destruction/cancellation must invalidate or detach future wake delivery safely.

### 6.3 Waitable

```c
#define CMETA_WAITABLE_METHODS(X,I) \
    X(I,R1,bool,arm,cmeta_waker,waker) \
    X(I,V0,void,cancel,_)
CMETA_INTERFACE(cmeta_waitable, CMETA_WAITABLE_METHODS);
```

A waitable represents one blocking reason, not a task itself.

Examples:

- timer expiry;
- file descriptor readable;
- IOCP completion;
- channel item available;
- child task completed;
- coordination condition became ready.

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

`Resumable` is the universal execution object. A CFlow source adapter, generator, relation, coroutine task and manually generated state machine may all expose this interface.

The executor knows only `Resumable`; it does not know whether the underlying implementation is minicoro, a generator, a relation coordinator or a compiler-generated switch machine.

## 7. Task model

### 7.1 Task state

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

Allowed state transitions in v1:

```text
NEW       -> RUNNABLE
RUNNABLE  -> RUNNING
RUNNING   -> RUNNABLE      cooperative yield
RUNNING   -> WAITING       await waitable
RUNNING   -> DONE
RUNNING   -> FAILED
RUNNING   -> CANCELLED     cancellation observed at checkpoint
WAITING   -> RUNNABLE      wake
WAITING   -> CANCELLED     cancellation + waitable cancellation/wake path
```

Forbidden transitions include:

- `DONE -> RUNNING`;
- `FAILED -> RUNNING`;
- `CANCELLED -> RUNNING`;
- foreign-thread `WAITING -> RUNNING` bypassing executor ownership.

Terminal states are immutable.

### 7.2 Typed task descriptor

CMeta Core provides a generic runtime descriptor plus generated concrete typed wrappers.

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

A generated `Task<T>` contains result storage with correct C alignment and type metadata. The public typed API must never use `void *` for successful results.

Conceptually:

```c
typedef struct cmeta_task_User {
    cmeta_task_base base;
    User result;
} cmeta_task_User;
```

The generic-kind layer may later expose this as `typed(Task, User)` or syntax `Task<User>`; v1 core does not depend on parser syntax.

## 8. Await and yield semantics

`yield` and `await` are different primitives.

### 8.1 Yield

`yield` means the task voluntarily relinquishes execution without depending on a concrete external condition.

```text
RUNNING -> RUNNABLE
```

The task is requeued onto the same executor.

### 8.2 Await

`await` means the task cannot progress until a `Waitable` fires.

```text
RUNNING
   ↓ arm waitable
WAITING
   ↓ coroutine/backend yields
executor continues
   ↓ waker invoked
RUNNABLE
```

The owning executor, not the wait callback, performs the next resume.

The coroutine backend provides stack suspension. CMeta Exec provides the semantic transition and ownership rules.

## 9. Cooperative cancellation

### 9.1 CancelToken

```c
typedef struct cmeta_cancel_token cmeta_cancel_token;

bool cmeta_cancel_requested(const cmeta_cancel_token *token);
void cmeta_cancel_request(cmeta_cancel_token *token);
```

Cancellation is cooperative in v1.

Mandatory cancellation checkpoints:

- before suspending in `await`;
- immediately after waking from `await`;
- explicit `task_cancel_point`;
- channel send/receive waits;
- timer waits;
- structured-scope child join waits.

A waiting task that is cancelled must cancel/detach its active waitable and be posted to its executor so that cleanup executes on the owning executor thread.

No OS thread cancellation mechanism is used.

## 10. Structured Scope

`Scope` owns spawned child tasks.

Core invariant:

> A scope cannot finish while a child task remains live. Scope exit either observes child completion or requests cancellation and joins child termination.

Proposed internal shape:

```c
typedef struct cmeta_scope {
    cmeta_task_base *owner;
    cmeta_task_base **children;
    size_t child_count;
    size_t child_capacity;
    bool closing;
} cmeta_scope;
```

v1 semantics:

- child task inherits the scope cancellation lineage;
- child executes on the scope owner's executor unless an explicit executor is supplied;
- exiting a scope normally waits for all children;
- exceptional/cancelled exit requests cancellation of remaining children, then waits for terminal state;
- no detached task is created through the scoped spawn API.

A separate explicit detached API may be added later but is not part of v1.

## 11. Coordination as a CMeta Exec algebra

The existing CFlow coordination modes become general execution primitives:

```c
typedef enum cmeta_coord_mode {
    CMETA_COORD_ALL,
    CMETA_COORD_ALL_DONE,
    CMETA_COORD_ANY,
    CMETA_COORD_LATEST,
    CMETA_COORD_SEQUENCE
} cmeta_coord_mode;
```

CMeta Exec coordination retains the current child-ownership semantics and typed retained values.

Higher-level mappings:

```text
ALL / ALL_DONE       -> join/all
ANY                  -> race
SEQUENCE             -> ordered fallback/sequence
LATEST               -> combine-latest style consumers
SEQUENCE + TRY_NEXT  -> first successful fallback
```

CFlow relation policies remain a CFlow-specific composition of coordination, completion, result and error policies. CMeta Exec exposes lower-level child coordination and task combinators without importing CFlow graph concepts into the executor.

Existing Lean proofs for coordination behavior should be reused or generalized rather than rewritten from scratch.

## 12. Coroutine backend ABI

The coroutine backend has one responsibility: preserve and resume the C call stack.

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

The exact opaque storage layout may differ, but the public semantic interface remains backend-neutral.

### 12.1 minicoro backend

The first backend maps these operations onto minicoro.

Rules:

- `user` points to CMeta task/frame state;
- `mco_push`/`mco_pop` are not used as the typed task transport;
- task result storage stays in CMeta-owned typed memory;
- minicoro status is translated into CMeta coroutine state;
- allocation configuration is routed through CMeta/backend configuration rather than exposed through Task APIs;
- minicoro-specific status codes are converted to CMeta runtime errors at the backend boundary.

The backend may be placed under `cmeta/src/coro/minicoro_backend.c` with any vendored header isolated under the project vendor policy. Public CMeta headers must not include `minicoro.h`.

## 13. Executor model

### 13.1 v1 ownership rule

A task is pinned to one executor for its lifetime.

```text
create -> executor E
all resume/yield/wait cleanup -> E
terminal cleanup -> E
```

A foreign thread may only notify/post to executor E. It does not resume the coroutine directly.

This rule avoids cross-thread stackful-coroutine migration in the first implementation.

### 13.2 Executor responsibilities

A concrete event-loop executor owns:

- ready queue;
- pending cross-thread posts;
- timer queue;
- poller instance;
- owner thread identity;
- shutdown state.

Conceptual loop:

```text
while running:
    drain cross-thread posts
    run bounded ready tasks
    process expired timers
    compute poll timeout
    poll platform events
    convert completions into waker calls/posts
```

The loop must avoid starvation by bounding one ready-drain quantum before returning to timers/poller.

### 13.3 Deterministic test executor

The existing logical-clock scheduler semantics remain valuable. CMeta Exec must retain a deterministic/manual-clock executor used by unit and Lean-conformance witnesses.

This executor is the reference implementation for state transition tests because it avoids real wall-clock and OS scheduling nondeterminism.

## 14. Thread model

Threads are execution resources beneath executors, not the task API.

v1 provides:

- single-thread event-loop executor;
- optional N independent executor threads;
- explicit post from one executor to another;
- no transparent task migration;
- no work stealing.

Cross-thread communication is by executor post, channel, cancellation request or platform completion delivery.

A future worker-pool backend may schedule non-coroutine pure jobs, but must not silently resume a pinned stackful coroutine on a different thread.

## 15. Platform abstraction

Platform-specific code is restricted to `cmeta/platform` implementations.

Split interfaces instead of one giant platform vtable:

```text
ThreadOps
ClockOps
PollerOps
WakeupOps
```

### 15.1 ThreadOps

Required operations:

- current thread identity;
- thread start;
- thread join;
- optional naming.

C11 threads may provide the generic implementation where adequate. Platform implementations can replace pieces where necessary.

### 15.2 ClockOps

Required operation:

```c
uint64_t cmeta_monotonic_ns(void);
```

No executor scheduling decision uses wall-clock/calendar time.

### 15.3 PollerOps

The poller API models readiness/completion as registrations that eventually wake CMeta waitables.

Conceptual operations:

```c
bool poller_init(...);
bool poller_register(...);
bool poller_modify(...);
bool poller_remove(...);
int  poller_wait(...);
void poller_wake(...);
void poller_destroy(...);
```

The public poller token must be platform-neutral and stable enough to associate a completion with a waitable owner.

### 15.4 Platform mappings

Initial targets:

```text
Linux       epoll + eventfd
Windows     IOCP + PostQueuedCompletionStatus-style executor wake
Darwin/BSD  kqueue + user-event/pipe wake
POSIX       poll + pipe fallback
```

WASM is not a v1 acceptance target for CMeta Exec. The coroutine backend ABI must not preclude it, but event-loop and Asyncify constraints should be handled in a later platform spec rather than weakening the native design.

## 16. State Machine IR

CMeta State is a finite typed graph domain.

### 16.1 Machine descriptor

Conceptual descriptor:

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

### 16.2 Event payloads

Each event descriptor has one logical payload CType. Events without payload use `void`/unit semantics at the descriptor layer.

A transition guard/action signature must be validated against machine context and the exact event payload.

The first implementation may choose one of two callable ABI shapes and use it consistently:

```text
guard  : (Context*, Payload) -> bool
action : (Context*, Payload) -> void/status
```

or generated capture wrappers that bind `Context*` and expose payload-only callables. The final ABI must reuse CMeta callable metadata and not create a second unrelated callable type system.

### 16.3 Guard contract

A guard must require:

```text
PURE + DETERMINISTIC + TOTAL
```

A guard with IO, state mutation, MAY_FAIL or UNKNOWN effect is rejected by machine validation.

### 16.4 Action contract

Actions may be stateful, IO-bearing or MAY_FAIL according to their declared CMeta contract.

A v1 transition action does not suspend. An action that needs asynchronous work starts/spawns a Task and returns. Task completion feeds a later typed event back into the machine.

This keeps machine transition execution finite and atomic with respect to the machine event loop.

### 16.5 Determinism

For a given `(state,event)`, v1 requires either:

- zero transitions;
- exactly one transition without a guard;
- multiple guarded transitions only if an explicit priority/order policy is introduced.

Because proving arbitrary guard mutual exclusion is not generally possible, v1 chooses the stricter rule:

> at most one transition per `(state,event)`.

This makes the machine deterministic by schema and keeps formal checking decidable.

Guard-based branching can be represented as an action that emits a more specific event, or introduced later with explicit ordered guards.

## 17. State machine runtime semantics

Event processing is serialized per machine instance.

For one accepted event:

```text
validate current state + event
        ↓
evaluate guard, if present
        ↓
run source exit hook, if later supported
        ↓
run transition action
        ↓
commit target state
        ↓
run target enter hook, if later supported
```

For v1, enter/exit hooks are not first-class syntax; the transition action is the only action. This avoids multiple action-order semantics until the flat machine core is stable.

If an action reports a runtime infrastructure error, the machine dispatch reports failure and does not silently apply a second transition. Whether state commits before or after action failure must be fixed. v1 chooses:

> execute action first; commit `target` only when action returns success.

Therefore a failed action leaves the machine in the source state.

Application-level failure that is intentionally part of the protocol should be modeled as an emitted typed event, not as an infrastructure error.

## 18. State Machine and Task integration

The integration rule is event-based, not suspension-inside-transition.

Example:

```text
Disconnected + Open(Address)
    -> Connecting
    action: spawn connect task

connect task completes Ok(Socket)
    -> emit Connected(Socket)

Connecting + Connected(Socket)
    -> Connected
```

A machine action may spawn a scoped or machine-owned task and arrange for task completion to enqueue a typed event onto the machine's executor/mailbox.

The task does not mutate the machine state directly from a foreign thread.

Machine event delivery occurs on the machine's owning executor, giving each machine instance serialized transition execution without a mutex around each transition.

This forms the basis for a later Actor abstraction:

```text
Actor = Machine + Mailbox<Event> + Executor affinity
```

Actor is intentionally outside v1.

## 19. Channel<T>

Channels are not required to land before Task and Executor, but the Exec ABI must support them naturally.

A generated typed channel uses CType metadata and concrete storage rather than `void *` payloads.

Conceptual operations:

```text
send(Channel<T>, T)
recv(Channel<T>) -> Task<T> / Waitable<T>
```

Cross-thread send enqueues data then posts/wakes the receiver's executor. It does not resume a coroutine on the sender thread.

Bounded channels later use waitables for both `not_empty` and `not_full` conditions.

## 20. CFlow migration plan at architecture level

The CFlow migration is intentionally staged.

### Stage A: compatibility extraction

Introduce CMeta Exec equivalents of:

- waker;
- waitable;
- step;
- resume context;
- resumable;
- coordination mode;
- deterministic executor interface.

CFlow public headers keep compatibility typedefs/wrappers where practical.

### Stage B: CFlow internal adoption

Move or adapt:

```text
cflow/runtime.c
cflow/coord.c
cflow/subrun.c
cflow/scheduler.c
cflow/scheduler_worker.c
```

to consume CMeta Exec primitives.

CFlow graph, lowering, optimization and plan semantics remain in `cflow/`.

### Stage C: ownership cleanup

After tests and conformance demonstrate equivalence, CFlow-specific duplicate implementations of generic scheduler/waitable/coordination code are removed or reduced to adapters.

The migration must preserve existing CFlow APIs until a deliberate compatibility decision is made. A broad API rename is not part of the first concurrency implementation.

## 21. Effect and property integration

CMeta already models effects:

```text
PURE
STATEFUL
ASYNC
IO
MAY_FAIL
UNKNOWN
```

and positive properties including `DETERMINISTIC`, `TOTAL`, `IDEMPOTENT`, `NO_ALIAS`, `ASSOCIATIVE`.

Concurrency introduces no replacement effect system.

Rules:

- coroutine/task callables normally carry `ASYNC`;
- I/O task producers carry `ASYNC | IO`;
- cancellation-aware operations may be `MAY_FAIL` if cancellation is represented as runtime failure, but typed cancellation results are preferred where practical;
- state machine guards require pure/stable contracts;
- parallel execution eligibility can later require `PURE + DETERMINISTIC + TOTAL`;
- optimizer rewrites must continue to use declared contracts rather than inferred extensional behavior.

No `THREAD_SAFE` or `SEND` property is introduced in v1. Executor affinity is a runtime ownership rule, not a user trait.

## 22. Memory and lifetime model

CMeta Exec does not introduce Rust-like ownership.

The runtime does define explicit object ownership rules:

- executor owns queued task scheduling references;
- scope owns live child relationships;
- task owns coroutine backend state and typed result storage;
- resumable owns its backend state after successful construction;
- coordination owns moved child resumables;
- an armed waitable owns or references its platform registration according to the waitable implementation;
- cancellation must detach/cancel the active waitable before task destruction;
- machine descriptors are immutable static/generated metadata;
- machine instance owns mutable context and current state;
- event payload ownership is defined by the generated event API or caller contract, not inferred by the state-machine engine.

Resource-bearing CTypes may later use destructor/copy/move traits, but v1 Exec must work with ordinary trivially copied C values first.

## 23. Error model

Three error classes remain distinct.

### 23.1 Infrastructure error

Examples:

- failed coroutine allocation;
- invalid resumable state;
- failed platform poll registration;
- impossible internal state transition.

Represented as `CMETA_STEP_ERROR` or executor/task runtime error.

### 23.2 Application typed error

Examples:

- HTTP failure;
- parse failure;
- connection refused.

Represented in normal typed task result data such as `Result<T,E>` or explicit event variants.

### 23.3 Cancellation

Cancellation is a terminal task state, not automatically equivalent to application error. Higher-level APIs may map cancellation into a typed result when desired.

This separation is required for correct structured concurrency and state-machine reasoning.

## 24. Formal verification strategy

Formal work is split by subsystem rather than proving one monolithic concurrent runtime.

### 24.1 State Core

Model finite states, events and transitions.

Required theorems:

- every admitted transition source/target belongs to the machine state universe;
- event payload signature is preserved;
- dynamic dispatch of a generated typed transition yields the statically known target state;
- deterministic schema has at most one transition per `(state,event)`;
- reachability calculation agrees with the finite transition graph;
- unreachable states can be identified without changing reachable transition semantics.

### 24.2 Task Core

Model task state transitions.

Required theorems:

- terminal task states cannot resume;
- only `RUNNABLE` can become `RUNNING`;
- `WAITING` returns to `RUNNABLE` only through wake/cancellation semantics;
- successful task completion carries the declared CType;
- cancellation cannot produce a successful result with a mismatched CType.

### 24.3 Coordination

Generalize existing CFlow proofs for:

- ALL;
- ALL_DONE;
- ANY;
- LATEST;
- SEQUENCE;
- failure policies where moved to generic Exec.

Existing executable C conformance should be retained to prevent the formal model from drifting from runtime behavior.

### 24.4 Coroutine backend conformance

Do not attempt to prove minicoro implementation internals.

Prove/validate the backend refinement boundary:

```text
CMeta task resume
  -> backend resume
  -> yield/completion observation
  -> CMeta task state transition
```

C conformance witnesses should exercise create/resume/yield/complete/cancel/destroy and compare observed traces with the Lean task-state model.

### 24.5 Platform conformance

Platform I/O scheduling is tested rather than kernel-proved. Formal claims stop at the Waitable/Waker contract. Each platform backend must satisfy tests showing that a registered completion eventually posts the matching executor wake in deterministic test harnesses where possible.

No fairness theorem is claimed for arbitrary OS scheduling.

## 25. Testing strategy

All implementation phases use CTest and the existing formal CI style.

### 25.1 Unit tests

State:

- event payload/type validation;
- deterministic transition rejection;
- target/source validation;
- action failure leaves source state unchanged;
- reachability/dead-state analysis;
- table backend and switch backend equivalence.

Exec:

- legal/illegal task transitions;
- yield requeue;
- wait/wake;
- cancel while runnable;
- cancel while waiting;
- terminal-state immutability;
- scope waits for children;
- scope cancellation propagates and joins.

Coroutine backend:

- stackful local variables survive yield/resume;
- nested C call can yield through backend abstraction;
- typed result survives completion;
- cancellation cleanup occurs on executor owner;
- repeated resume after terminal state is rejected by CMeta layer.

Platform:

- monotonic clock sanity;
- executor self-wake;
- cross-thread post;
- timer wake;
- platform poller smoke test.

### 25.2 Conformance generators

Follow the current pattern:

```text
real C implementation
   -> deterministic generated Lean snapshot
   -> Lean theorem
```

Separate generators are preferred for State, Task and coroutine backend so failures identify one layer.

### 25.3 Cross-platform CI

The runtime acceptance matrix must eventually include:

```text
Linux
Windows
macOS
```

Android/iOS can follow once native desktop backends stabilize. WASM is explicitly deferred.

## 26. File/module boundaries

Target structure:

```text
cmeta/
  include/cmeta/
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
    state/
      machine.h
      event.h
      transition.h
      validate.h
      exec.h
    coro/
      backend.h
    platform/
      thread.h
      clock.h
      poller.h

  src/
    exec/
      executor.c
      task.c
      cancel.c
      scope.c
      coord.c
    state/
      validate.c
      exec.c
    coro/
      minicoro_backend.c
    platform/
      linux/...
      windows/...
      darwin/...
      posix/...
```

The exact CMake source list may evolve, but the responsibility boundaries above are normative.

`cmeta/CMakeLists.txt` must grow from its current single-source layout without turning `src/cmeta.c` into a catch-all concurrency implementation.

## 27. Delivery decomposition

This umbrella architecture is deliberately split into four implementation subprojects. Each subproject must reach a green, usable state before the next one depends on it.

### Subproject 1: CMeta State Core

Deliver:

- finite machine descriptors;
- typed event descriptors;
- transition validation;
- deterministic dispatch;
- reachability analysis;
- table backend;
- Lean static/dynamic checker model;
- C implementation conformance.

No coroutine or thread dependency.

### Subproject 2: CMeta Exec Core

Deliver:

- Step/Waker/Waitable/Resumable extraction;
- deterministic executor;
- task state model;
- cooperative cancellation;
- Scope;
- generic coordination extraction;
- CFlow compatibility adapters;
- Lean task-state and coordination model.

No minicoro dependency required yet.

### Subproject 3: minicoro Coroutine Backend

Deliver:

- backend ABI;
- minicoro adapter;
- stackful Task runner;
- yield/await bridge;
- cancellation cleanup;
- conformance trace against Task model.

No production OS poller required; deterministic executor and synthetic waitables are sufficient for acceptance.

### Subproject 4: Native Platform Executor

Deliver:

- Linux epoll/eventfd implementation;
- Windows IOCP implementation;
- Darwin kqueue implementation;
- timer integration;
- cross-thread executor post;
- native asynchronous waitable examples;
- Linux/Windows/macOS CI.

CFlow can then migrate worker/event-loop uses incrementally.

## 28. Compatibility policy

During extraction:

- existing CFlow tests and formal conformance remain mandatory;
- no existing CFlow public runtime symbol is removed in the same change that introduces CMeta Exec;
- compatibility typedefs/wrappers may temporarily map `cflow_*` primitives to `cmeta_*` primitives;
- once all CFlow internals use CMeta Exec and compatibility coverage is green, public deprecation/removal can be proposed separately.

This prevents concurrency work from destabilizing the already verified CFlow subset.

## 29. Acceptance criteria for the architecture

The architecture is considered successfully implemented when all of the following are true:

1. A flat typed CMeta state machine can be declared/generated, validated and executed without CFlow.
2. A `Task<T>` can run through generic CMeta Exec with deterministic executor semantics without minicoro.
3. The same Task abstraction can run through the minicoro backend without changing the Task API.
4. A waiting task is resumed only by its owning executor after a waker posts readiness.
5. A cancelled waiting task detaches its waitable and reaches terminal cancellation without foreign-thread coroutine resume.
6. A `Scope` cannot exit with live children.
7. Existing CFlow relation/runtime tests and Lean conformance continue to pass after CFlow begins consuming CMeta Exec.
8. State-machine task integration uses typed completion events rather than direct foreign-thread state mutation.
9. Linux, Windows and macOS native executor backends expose the same public Exec API.
10. Lean formalization proves the finite State Core and Task state-transition invariants, while C-generated conformance snapshots connect those models to the actual C implementation.

## 30. Design principle summary

The design is intentionally not Rust ownership, Zig comptime or a new language runtime.

CMeta remains:

```text
finite typed description
+ explicit effects/properties
+ generated ordinary C ABI
+ typed execution IR
+ formalizable state transitions
```

State machines answer:

> What logical state is the system in, and which typed event may change it?

Tasks/coroutines answer:

> What computation is currently runnable, waiting or complete?

Executors/threads answer:

> Where and when may that computation resume?

The coroutine backend answers only:

> How is the C call stack suspended and restored?

Keeping those questions separate is the central invariant of the architecture.