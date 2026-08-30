# CFlow Reactive Public API Redesign

## Decision

CFlow exposes two high-level asynchronous models: Actor and Reactive. Reactive
uses the standard public roles `Publisher`, `Subscriber`, and `Subscription`.
The generic execution engine, demand pump, wait protocol, and native adapter
state remain implementation details rather than user-selectable models.

This is an intentional CFlow ABI v4 break. The removed C API is not retained as
aliases, wrappers, deprecated headers, or installed compatibility shims.

## Public contract

`<cflow/reactive.h>` owns the Reactive protocol:

```c
typedef struct cflow_subscription {
    void *impl;
} cflow_subscription;

bool cflow_subscribe(
    cflow_subscription *subscription,
    const cflow_graph *graph,
    cflow_publisher *publisher,
    cflow_scheduler *scheduler,
    const cflow_subscriber *subscriber);

bool cflow_subscription_request(cflow_subscription *subscription, size_t count);
void cflow_subscription_cancel(cflow_subscription *subscription);
void cflow_subscription_close(cflow_subscription *subscription);
```

Successful subscribe moves and clears the Publisher. The Subscription owns all
live graph execution state, borrows the immutable Graph, Scheduler, Subscriber
callbacks, type descriptors, and adapter context, and must close before those
borrows end. Demand always counts downstream values. Cancellation and close
retain the existing synchronous/quiescent guarantees.

`<cflow/publishers.h>` contains array, Range, timer, channel, and readiness
Publisher factories. `<cflow/io_publisher.h>` contains the bounded Actor-backed
I/O Publisher and owner-driving API. Platform readiness registration factories
also produce Publishers. Actor and Reactive are the only public asynchronous
execution models.

## Naming boundary

The following names are removed from the installed generic Reactive API:

- `cflow_source` and every `cflow_source_*` factory or operation;
- `cflow_run` and every `cflow_run_*` operation;
- `cflow_sink` and its callback adapter;
- the installed `runtime.h`, `sources.h`, and `io_source.h` headers.

Graph input nodes, source-code locations, single fact origins, and parser inputs
retain their precise meanings. Stateful Machine and Statechart execution is
exposed as an Instance, not as another execution model or compatibility alias.

## Ownership and errors

- Publisher construction requires a zero-state destination and never overwrites
  a live Publisher.
- Successful subscribe moves the Publisher exactly once; failed admission leaves
  it caller-owned.
- Subscriber values are borrowed only during the callback.
- Every successful native operation admission reaches one terminal completion,
  cancellation, or explicit error before its payload borrow ends.
- Subscription close preserves the first authoritative error and completes all
  owned cleanup before clearing the handle.
- Fixed capacities and backpressure semantics remain unchanged.

## Documentation and benchmark policy

User documentation describes `Direct`, `Actor`, and `Reactive`. It may mention
exact Publisher, Subscriber, and Subscription API identifiers, but does not
present the execution pump or its former names as a product layer. Historical
design records remain factual records and are not rewritten mechanically.

The NativeIO adapter benchmark reports `NativeIO Direct`, `Actor`, and
`Reactive`. Its Reactive path must call the public subscription functions. It
may collect internal stage counters, but table names and explanations cannot
expose implementation-only roles.

## Verification

- The aggregate C and C++ headers compile with Publisher, Subscriber, and
  Subscription and without any removed header dependency.
- Reactive behavior tests cover move admission, demand, value delivery,
  wait/wake, cancellation, error, and close.
- Array, Range, timer, channel, readiness, I/O, Machine, Statechart, temporal,
  stream evaluation, CFlow-FS, examples, and benchmarks compile against the new
  API.
- Installed-package verification proves removed headers are absent and new
  headers are usable.
- Windows Release and remote Linux Release run the nearest contract tests and
  NativeIO adapter benchmark.
