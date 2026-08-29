# CFlow Execution Policy Architecture Correction

**Date:** 2026-08-29  
**Status:** committed-spec review gate  
**Amends:** `2026-08-22-cflow-execution-foundation-design.md`

## Scope

This document corrects one over-broad interpretation of the CFlow execution
architecture. It changes the architecture contract only. It does **not** change
production code, public API/ABI, native backend behavior, Actor behavior,
Scheduler behavior, benchmark code, or package exports.

The correction is intentionally written and committed before any production
fix. Production work must not start until this amendment passes review.

## Counterexample: release benchmark workflow #266

CFlow release host benchmark workflow
[#266](https://github.com/qigao/turbo-utils/actions/runs/33237820357) completed
successfully on `master@68b6aa6a9d2e817061c8512cadff52aba77ddc0f` and
published host evidence for Linux, macOS, and Windows. The evidence contains
first-class comparisons for both:

- direct execution versus Actor-mediated execution; and
- blocking versus busy native driving.

That evidence is a counterexample to any architecture rule that treats Actor,
Run, Scheduler, or one wait strategy as a mandatory correctness substrate for
all execution. These are intentionally different execution strategies with
observable cost and scheduling characteristics. The benchmark exists precisely
because they are not semantically required to collapse into one universal path.

The evidence also must not be over-read: workflow #266 is performance evidence,
not a proof that direct, Actor, blocking, or busy is universally fastest. Its
architectural force is narrower and stronger: **multiple execution policies are
valid first-class implementations of the same higher-level semantics, so one
policy cannot be promoted into a universal invariant merely because it is a
convenient composition layer.**

## Rejected interpretation

The following interpretations are invalid:

```text
Actor == one dedicated thread
```

and, more generally:

```text
valid async/control execution
    -> must pass through Actor
    -> must pass through Run/Scheduler
    -> must therefore pay those layers' dispatch/synchronization cost
```

Likewise, Graph IR does not select a thread model. A Graph specifies **what** is
executed; execution policy specifies **where and how** ready work executes.

Actor is a user/runtime model composed from semantic state, bounded admission,
serialized mutation, and lifecycle. Thread ownership is not part of Actor
identity.

## Corrected invariant

### 1. Single mutation owner, not single Actor thread

For every mutable semantic object, there is exactly one logical mutation owner.
For Machine/Statechart/Actor-style state, mutation is serialized through a
`SerialExecutor` boundary (or an equivalently proven serialized executor
contract). Concurrent producers may submit work, but they do not mutate the
private semantic state directly.

The executor may itself be backed by shared worker threads, explicit manual
driving, or another permitted execution policy. Therefore:

```text
one semantic mutation owner != one dedicated OS thread
```

### 2. Pay for what you use

CFlow execution layers are capabilities, not mandatory toll booths. A caller
uses the narrowest execution surface that provides the semantics it needs.
Conceptually:

```text
Direct < Plan < Run
```

where the relation means increasing execution/runtime semantics and therefore
potential overhead, not semantic superiority.

A fully static/synchronous path may collapse to direct C execution. A compiled
Plan may add reusable lowering/optimization without long-lived Runtime state. A
Run is required when demand, WAIT/wake, cancellation, long-lived Source state,
or asynchronous lifecycle are part of the contract. Actor is required only
when Actor semantics themselves are required: identity, bounded concurrent
admission, serialized private-state mutation, and Actor lifecycle.

No higher layer may become a hidden prerequisite for a lower layer merely to
reuse its machinery.

### 3. No hidden concurrency

The invariant across all policies is not “everything uses Scheduler” or
“everything uses Actor.” The invariant is that concurrency and ownership are
explicit and bounded:

- the mutable fact source and its single mutation owner are named;
- queue/admission capacity and backpressure are explicit;
- the driver/executor responsible for progress is explicit;
- completion authority and acknowledgement/release ownership are explicit;
- cancellation and shutdown/drain rules are explicit;
- callback thread/reentrancy rules are explicit;
- no hidden dedicated service thread, unbounded queue, implicit retry, or
  backend downgrade is introduced by composition.

A direct/manual path that satisfies those rules is architecturally valid. A
Scheduler/Run/Actor path that satisfies those rules is also architecturally
valid.

### 4. Performance policy is not a semantic fact source

`direct` versus `Actor`, `blocking` versus `busy`, and future executor/backend
choices are performance/execution policies. They may be benchmarked and chosen
for a workload, but none may redefine Graph semantics, semantic ownership, or
completion truth.

Performance evidence may justify an implementation choice. It does not justify
moving semantic ownership into a faster path or forcing every caller through a
slower abstraction.

## Consequence for Actor architecture

Actor remains:

```text
semantic state
    + bounded Mailbox/admission
    + SerialExecutor mutation boundary
    + concurrent producers
    + explicit lifecycle
```

The important boundary is:

```text
producer threads
    -> bounded admission
    -> shared/selected SerialExecutor
    -> exactly one semantic mutation stream
```

not:

```text
Actor
    -> private dedicated thread
```

This keeps Actor compatible with a shared Executor pool while preserving the
same state-transition ownership proof obligations.

## Consequence for Direct / Plan / Run

The execution architecture must preserve distinct first-class paths:

```text
static work
    -> Direct execution when no runtime semantics are required

compiled reusable work
    -> Plan when compiled/lowered execution is required

reactive / demand-driven / cancellable work
    -> Run + Source/Waitable/Scheduler when those semantics are required

concurrent identity + mailbox + serialized private state
    -> Actor when Actor semantics are required
```

Adapters may compose these layers, but composition must remain visible. An
adapter must not quietly turn an optional higher-level model into the only way
to reach a lower-level backend.

## Compatibility with the execution foundation

This correction strengthens rather than replaces the existing execution
foundation:

- `Executor` still answers where and under what serialization policy a ready
  task executes;
- `TimerQueue` still answers when delayed work becomes ready;
- `Scheduler` remains a compatibility/policy composition surface;
- `SerialExecutor` remains the default semantic mutation boundary for
  Machine/Actor-style state;
- `ManualExecutor`, `WorkerExecutor`, and optional narrow inline execution
  remain valid policies with their documented restrictions.

The corrected reading is that none of those policy objects implies a
one-object/one-thread topology, and none is universally mandatory outside the
semantics it provides.

## Review gate

Before any production correction is planned or implemented, review must confirm
all of the following:

1. workflow #266 is recorded only as a counterexample to mandatory policy
   mediation, not as a universal performance claim;
2. Actor's invariant is single serialized mutation ownership, not dedicated
   thread ownership;
3. `Direct < Plan < Run` expresses pay-for-used-semantics rather than a required
   promotion chain;
4. Graph/semantic truth remains independent of execution/thread policy;
5. no production file, public contract, or backend behavior is changed by this
   amendment.

Until this gate is approved, production remains unchanged.
