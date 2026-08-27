# CFlow Stream `count` Design

## Context

Issue #125 asks for a common Stream `count` terminal with C/C++ compile
coverage, empty/boundary/error tests, interpreted parity, and explicit managed
value semantics. The existing terminal adapters execute a bound `cmeta_range`
through the normalized CFlow graph and resumable runtime. `count` must use that
same path so filters and positional operators affect the result and source or
callback failures are not hidden.

This change does not add an XML parser, a Graph opcode, or a compiled-plan
fallback. XML/SCXML is outside #125, and a terminal accumulator is execution
state rather than graph topology.

## Public API

Add the compatible CFlow adapter:

```c
bool cflow_eval_count(const cflow_stream *stream,
                      size_t *out_count,
                      const char **out_error);
```

Add a TurboSTL result facade and function:

```c
typedef struct turbostl_count_result {
    bool ok;
    const char *error;
    size_t count;
} turbostl_count_result;

turbostl_count_result
turbostl_stream_count(const turbostl_stream_t *stream);
```

The prefixed function is the user operation. A global `count(...)` macro is
intentionally omitted because it would intercept C++ calls such as
`std::count(...)` and break source compatibility for a header advertised to C
and C++ consumers.

## Semantics

- The result is the number of values accepted by the terminal after all Graph
  operators. Empty input succeeds with zero.
- Execution is sequential and interpreted through `cflow_run`; the terminal
  neither reorders values nor short-circuits.
- A finite source completes normally. An unbounded source requires an upstream
  bounding operator such as `take`; `count` itself cannot make it terminate.
- The terminal borrows each live value only during the sink callback and never
  copies, moves, retains, or destroys it. Managed values therefore follow the
  existing interpreted runtime lifecycle.
- The implementation does not use a `SIZED` Range shortcut because filter,
  map, skip, take, callbacks, and source errors can change observable results.
- `out_count` is required and is reset to zero before validation. It is
  published only after successful completion; invalid arguments, Range
  admission failure, runtime failure, callback failure, or `size_t` overflow
  return `false` and leave it zero.
- `out_error` is optional. When supplied, it is reset to null and receives a
  borrowed diagnostic with the same lifetime rules as current CFlow runtime
  errors. Count overflow reports `stream count overflow`.
- A Stream whose Graph construction already failed is rejected before Source
  admission with its retained Stream error. If the initial maximum demand is
  exhausted without a terminal signal, the adapter requests one probe value:
  DONE publishes the exact maximum, while another value reports overflow.
- Repeated evaluation owns a fresh accumulator and is valid only when the
  bound Range satisfies its existing reusable contract.

## Architecture and compatibility

The adapter constructs a checked Range Source, then supplies a small counting
sink to the existing `cflow_eval_source` helper. The accumulator remains local
to the terminal invocation. The Graph and bound Range remain borrowed; the
Source and runtime execution state remain owned by the invocation. The helper
consumes the constructed Source on every success or failure path, including
Graph normalization failure.

The change is additive: existing structures, graph opcodes, terminal
signatures, error codes, and compiled-plan behavior are unchanged. It adds no
dependency and no format or deployment change.

## Verification

Tests cover CFlow empty/full/transformed counts, repeated evaluation, invalid
arguments, Range execution failure, and failure-output rollback. TurboSTL C
and C++ tests cover the public facade, including coexistence with
`std::count`. Managed-value tests assert that counting adds no terminal copy or
move. The installed-package consumer compiles and links the exported API.
