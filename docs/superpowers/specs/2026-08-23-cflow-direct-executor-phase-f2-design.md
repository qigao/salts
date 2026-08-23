# CFlow Direct Executor Phase F-2 Design

## Background

PR #30 defines Direct as the synchronous, closed, linear execution path whose
cost has one memory pass and zero callback dispatches. The existing
`cflow_plan_eval_array` path does not meet that contract: it executes decoded
plan instructions through step handlers and erased CMeta callables, while the
Kernel path interprets Graph IR. Phase F-2 adds the first production Direct
path without changing either existing executor.

## Decision

Phase F-2 uses an ISO C11 compile-time pipeline schema. One named schema is the
single source for both:

- a Surface Graph builder, preserving the existing Graph -> Optimize -> Plan ->
  Kernel verification chain; and
- a fused native array loop which invokes the same-TU `typed_call(name)` symbols
  directly.

The initial operator set is deliberately closed to non-capturing, named,
`value`-contract Filter and Map callables. FlatMap, Reduce, relations, capturing
lambdas, bound callables, cross-translation-unit typed declarations, scheduler
boundaries and asynchronous sources are ineligible in this phase.

The public form is:

```c
#define ExampleDirectSteps(M) \
    CFlowDirectSteps(M, \
        (filter, int, int, keep_even), \
        (map, int, long, square), \
        (map, long, double, half))

cflow_direct_pipeline(example, int, &cmeta_type_int,
                      double, 3, ExampleDirectSteps);
```

This generates `example_eligible`, `example_build` and `example_eval_array` as
translation-unit-local functions. `CFlowDirectSteps` indexes each row and the
generator wires every stage only to the immediately preceding generated value;
callers cannot name an older value and diverge from the linear Surface Graph.
The literal stage count is checked against the schema, while row C types and
callable signatures are subject to compile-time exact-type checks. The explicit
source descriptor avoids a second type registry and keeps custom CMeta type
support possible.

## Alternatives Considered

### Runtime Graph specialization or JIT

Rejected for Phase F-2. Runtime Graph nodes retain erased callable adapters;
ordinary portable C11 cannot recover a direct call instruction from arbitrary
runtime data. A JIT would introduce executable-memory policy, platform backends,
cache invalidation, security review and a new dependency boundary.

### Test-only handwritten fused loop

Rejected because it would prove only that C can execute a loop, not that CFlow
offers the Direct path defined by the calculus.

### Generate only a Direct loop

Rejected because a separately handwritten Surface Graph could drift from the
measured Direct program. Replaying one schema into both forms keeps equivalence
tests tied to one fact source.

## Execution and Ownership Protocol

- **Data unit:** one value of the declared source C type, transformed through a
  linear sequence into zero or one value of the declared output C type.
- **Fact source:** the named compile-time schema. Generated Graph and Direct
  code are derived views.
- **Input ownership:** borrowed immutable array for the duration of the call.
- **Output ownership:** caller-owned bounded array. Direct never allocates,
  retains or destroys it.
- **Result count:** caller-owned `size_t`; on a valid call it is set to zero
  before iteration and then to the committed output count.
- **Lifetime:** no borrowed address survives the call or an iteration.
- **Thread topology:** single-threaded per invocation. Independent generated
  pipelines or disjoint buffers may be called concurrently.
- **Order:** encounter order is preserved.
- **Capacity:** Filter/Map cardinality is at most the input count. A non-empty
  invocation requires `output_capacity >= input_count`; failure is detected
  before writes, removing a capacity branch from the hot loop.
- **Aliasing:** input, output and result-count storage must be disjoint. Overlap
  is rejected before execution.
- **Traits:** all participating value descriptors require trivial copy and
  trivial destroy. Phase F-2 therefore uses ordinary assignment without hidden
  lifecycle callbacks.
- **Failure:** invalid pointers, checked-size overflow and overlapping storage
  share `CFLOW_DIRECT_INVALID_ARGUMENT`; ineligible callables and insufficient
  capacity have distinct statuses. The executor does not partially process or
  silently select another path.
- **Shutdown:** none; the call owns no persistent resource or worker.

## Eligibility

`<name>_eligible()` resolves metadata once outside the hot loop and requires:

- a valid one-argument value-protocol callable for every row;
- zero capture bytes;
- pure effects;
- deterministic, total and no-alias properties (the `value` contract);
- valid descriptors with trivial copy and trivial destroy;
- Filter returning `bool`; and
- the declared source descriptor matching the first callable input.

The generated Surface builder checks the same predicate before initializing the
stream. The generated evaluator does the same before touching caller data.

## Architecture and Compatibility Impact

The change adds an installed `cflow/direct.h` header and includes it from the
aggregate `cflow/cflow.h`. It does not change the ABI, representation or behavior
of Graph, Plan, Kernel, Scheduler or CMeta callables. All generated pipeline
functions are `static`, so schemas do not add exported binary symbols.

Existing clients remain source- and binary-compatible. C clients opt in by
declaring a schema. C++ clients can continue including the aggregate header; the
C-only generation macro is hidden when `__cplusplus` is defined.

Migration cost is local: code that wants Direct defines a schema in the same
translation unit as its named typed callables and replaces an owned
`cflow_result` execution call with a caller-sized output array. Existing Plan or
Kernel calls remain valid.

## Verification

Correctness tests will derive a Graph and Direct loop from one schema, then
compare Direct observations with both Plan and Kernel. Boundary tests cover
empty input, capacity failure, alias rejection and ineligible contracts. A trap
callable whose erased adapter records invocation will prove behaviorally that
Direct uses `typed_call` and performs zero erased callback dispatches.

A separate benchmark target compares the generated Direct evaluator with Plan
for the same pipeline and data. Setup is outside timed regions; output validity
is checked outside timing. Reported performance is evidence only for the tested
compiler, build mode and workload.

## Rollback

The feature is isolated to one public header, its aggregate include, tests,
benchmark wiring and documentation. Removing those additions restores the prior
API without data migration, ABI change or persistent state conversion.
