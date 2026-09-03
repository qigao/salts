# CFlow Stream terminal operations

## Context

Issue #125 asks for `count`, `any_match`, `all_match`, `find_first`, and
`for_each` without turning CMeta into an algorithm runtime or making Stream a
Java compatibility layer. Existing bound Streams borrow a `cmeta_range`; each
terminal evaluation creates a fresh `cflow_run` and drives it with the test
scheduler used by the current synchronous adapters.

## Decision

The five operations are terminal adapters in `cflow/adapters.h`. They do not
add Graph nodes and do not mutate the reusable Stream Graph.

- `count` increments a checked `size_t` counter and fails on overflow.
- `any_match` and `all_match` accept the existing typed
  `cflow_filter_callable`, validate it against the Stream output type before
  source admission, and invoke it over borrowed live values.
- `find_first` returns an opaque, zero-initialized `cflow_find_result`. A found
  value is copy-constructed into independently owned, correctly aligned
  storage. `cflow_find_result_destroy` destroys that value exactly once.
- `for_each` accepts the existing `cflow_value_fn` visitor shape. A `false`
  return is an explicit terminal callback failure.

The output pointer of every scalar terminal is written only on success. A
failed operation leaves it in its documented zero value. `find_first` refuses
to overwrite a non-zero result handle.

Library-generated error strings have static storage. Source/runtime-provided
error strings are borrowed and remain subject to the source contract; callers
must not free any `out_error` value. Structured `_result` variants classify the
same operation with `cflow_status_result`; their canonical message accessors
always return library-owned process-lifetime text and never retain a foreign
diagnostic.

## Execution and short circuit

`count`, `all_match` without a mismatch, and `for_each` require a finite or
externally completing source. Empty input produces count zero, any false, all
true, no found value, and zero visitor calls.

`any_match` stops on the first true result, `all_match` stops on the first false
result, and `find_first` stops after retaining the first output. Their Sink
callbacks request cancellation of only the current Run and return success for
the decisive value. The terminal driver recognizes only its own marked
cancellation as successful short circuit. External cancellation and arbitrary
Sink rejection remain failures.

This is interpreted Runtime execution. Compiled Plans remain source-independent
byte-plan APIs and do not acquire bound-Stream terminal state or managed-value
ownership in this change. There is no interpreter fallback from Plan execution.

## Ownership and state

- Stream Graph and Range: borrowed and unchanged.
- predicate/action: copied into stack-local terminal state for the evaluation.
- counters and match flags: owned by that evaluation.
- values received by Sink callbacks: borrowed only during the callback.
- found value: owned by `cflow_find_result` until destroy.
- scheduler, source projection, and Run: created and closed by the terminal.

No terminal result or counter is shared between evaluations. Repeated
evaluation remains governed by the source Range's `REUSABLE` and version
contracts.

## Errors and compatibility

Invalid Stream, missing bound Range, invalid output pointer, predicate type
mismatch, unsupported lifecycle traits, callback failure, allocation failure,
source failure, and runtime failure all fail fast. Existing `cflow_eval_*`
entry points retain their behavior. Container exposes prefixed inline wrappers,
avoiding global macros such as `count` that would collide with container APIs.

The new opaque `cflow_find_result` is ABI-stable as a single implementation
pointer. Adding free functions is source and binary compatible with existing
consumers.

## Verification

Tests cover transformed Streams, empty input, repeat evaluation, decisive
short circuit before a later source error, predicate and visitor failure,
managed-value copy/destroy balance, C++ header use, and installed-package
consumption. Release and AddressSanitizer runs are required before completion.
